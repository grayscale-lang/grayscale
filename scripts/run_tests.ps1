<#
    run_tests.ps1 - Grayscale integration test runner for Windows.

    The PowerShell counterpart to run_tests.sh, with the same test categories,
    the same pass/fail rules, and the same exit codes.

      - Tests under pass/ must run successfully and must not print
        "SOME TESTS FAILED".
      - Tests under pass/warnings/ must emit the warning code named in
        their filename.
      - Tests under fail/ must fail.

    Usage (from the repo root):
      .\scripts\run_tests.ps1
      .\scripts\run_tests.ps1 -Verbose      # show output for failures
      .\scripts\run_tests.ps1 -NoBuild      # skip the rebuild
      .\scripts\run_tests.ps1 -Filter core  # only categories matching a pattern

    Author:  Aristomedes (@Aristomedes)
    Copyright (c) 2025-Present Marshall A Burns
    Licensed under the MIT License. See LICENSE for details.
#>

[CmdletBinding()]
param(
    [switch]$NoBuild,
    [string]$Filter = '',
    [int]$TimeoutSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$TestDir  = Join-Path $GrayRepoRoot 'integration-tests'
$GrayBin  = Join-Path $GrayRepoRoot 'gray.exe'
$GraycBin = Join-Path $GraycDir 'grayc.exe'

$script:PassCount = 0
$script:FailCount = 0
$script:Failures  = @()

function Write-Pass {
    param([string]$Name)
    $script:PassCount++
    Write-Host "  PASS  $Name" -ForegroundColor Green
}

function Write-Fail {
    param([string]$Name, [string]$Reason, [string]$Output = '')
    $script:FailCount++
    $script:Failures += "$Name $Reason"
    Write-Host "  FAIL  $Name $Reason" -ForegroundColor Red
    if ($VerbosePreference -eq 'Continue' -and $Output) {
        ($Output -split "`n" | Select-Object -First 15) | ForEach-Object {
            Write-Host "          $_" -ForegroundColor DarkGray
        }
    }
}

function Invoke-GrayTest {
    <#
        .SYNOPSIS
        Run gray against a file with a timeout, returning its output and exit
        code.

        .DESCRIPTION
        There is no `timeout` command to lean on here, so the process is
        started directly and killed if it overruns. A test that hangs is a
        failure, not something that should stall the whole run.
    #>
    param([string[]]$Arguments)

    $stdout = [System.IO.Path]::GetTempFileName()
    $stderr = [System.IO.Path]::GetTempFileName()
    try {
        $proc = Start-Process -FilePath $GrayBin -ArgumentList $Arguments -PassThru `
            -NoNewWindow -RedirectStandardOutput $stdout -RedirectStandardError $stderr

        # Reading .Handle forces the process handle to be cached. Without it a
        # -PassThru process started without -Wait exposes an empty .ExitCode
        # once it exits, which reads as $null and compares unequal to 0 -- so
        # every test would report an execution error regardless of outcome.
        $null = $proc.Handle

        if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
            try { $proc.Kill() } catch { }
            return [pscustomobject]@{ Output = "timed out after ${TimeoutSeconds}s"; ExitCode = 124 }
        }
        # The timed overload can return before the exit code is settled.
        $proc.WaitForExit()

        $text = [string](Get-Content -Raw -ErrorAction SilentlyContinue $stdout) +
                [string](Get-Content -Raw -ErrorAction SilentlyContinue $stderr)
        return [pscustomobject]@{ Output = [string]$text; ExitCode = $proc.ExitCode }
    } finally {
        Remove-Item -Force -ErrorAction SilentlyContinue $stdout, $stderr
    }
}

function Test-ShouldPass {
    <# Run a program that must succeed and must not report failed assertions. #>
    param([string]$Label, [string[]]$Arguments)

    $r = Invoke-GrayTest -Arguments $Arguments
    if ($r.ExitCode -ne 0) {
        Write-Fail $Label '(execution error)' $r.Output
    } elseif ($r.Output -match 'SOME TESTS FAILED') {
        Write-Fail $Label '(assertions failed)' $r.Output
    } else {
        Write-Pass $Label
    }
}

function Test-ShouldFail {
    <# Run a program that must be rejected. #>
    param([string]$Label, [string[]]$Arguments)

    $r = Invoke-GrayTest -Arguments $Arguments
    if ($r.ExitCode -eq 0) {
        Write-Fail $Label '(expected error, got success)' $r.Output
    } else {
        Write-Pass $Label
    }
}

function Get-MainFile {
    param([string]$Directory)
    Get-ChildItem -Path $Directory -Recurse -Filter 'main.gray' -File |
        Select-Object -First 1 -ExpandProperty FullName
}

function Should-Run {
    param([string]$Category)
    return (-not $Filter) -or ($Category -like "*$Filter*")
}

# ---- Build ----------------------------------------------------------------

# Every pass/ test compiles a program, so grayc has to be able to find a C
# compiler. MinGW and MSYS2 do not put themselves on PATH, so resolve one and
# prepend its directory -- otherwise each test dies with "no C compiler found"
# while the check-only warning tests still pass, which makes the failure look
# like a compiler bug rather than a missing toolchain.
$null = Resolve-GrayCC

if (-not $NoBuild) {
    Write-Host 'Building grayc...'
    Push-Location $GraycDir
    try {
        Invoke-GrayNative { & make build }
        if ($LASTEXITCODE -ne 0) { Write-Host 'grayc build failed' -ForegroundColor Red; exit 1 }
    } finally { Pop-Location }

    Write-Host 'Building gray CLI...'
    $go = Resolve-GrayGo
    New-GrayEmbedStubs
    Push-Location $GrayRepoRoot
    try {
        Invoke-GrayNative { & $go build -o $GrayBin ./cli }
        if ($LASTEXITCODE -ne 0) { Write-Host 'gray build failed' -ForegroundColor Red; exit 1 }
    } finally { Pop-Location }
}

# Point gray at the freshly built compiler rather than any system install.
$env:GRAY_COMPILER_PATH = $GraycBin

Write-Host ''
Write-Host '========================================'
Write-Host '  Grayscale Integration Test Suite'
Write-Host '========================================'
Write-Host ''

# ---- pass/ ----------------------------------------------------------------

Write-Host 'PASS tests:'

foreach ($category in @('core', 'stdlib')) {
    if (-not (Should-Run $category)) { continue }
    $dir = Join-Path $TestDir "pass\$category"
    if (-not (Test-Path $dir)) { continue }
    foreach ($f in Get-ChildItem -Path $dir -Filter *.gray -File) {
        Test-ShouldPass "$category/$($f.BaseName)" @($f.FullName)
    }
}

if (Should-Run 'multi-file') {
    $dir = Join-Path $TestDir 'pass\multi-file'
    if (Test-Path $dir) {
        foreach ($d in Get-ChildItem -Path $dir -Directory) {
            $main = Get-MainFile $d.FullName
            if ($main) { Test-ShouldPass "multi-file/$($d.Name)" @($main) }
        }
    }
}

if (Should-Run 'new') {
    $dir = Join-Path $TestDir 'pass\new'
    if (Test-Path $dir) {
        Write-Host ''
        Write-Host 'NEW template tests:'
        foreach ($f in Get-ChildItem -Path $dir -Filter *.gray -File) {
            Test-ShouldPass "new/$($f.BaseName)" @($f.FullName)
        }
        foreach ($d in Get-ChildItem -Path $dir -Directory) {
            $main = Get-MainFile $d.FullName
            if ($main) { Test-ShouldPass "new/$($d.Name)" @($main) }
        }
    }
}

if (Should-Run 'stress') {
    $printed = $false
    foreach ($sub in @('core', 'stdlib')) {
        $dir = Join-Path $TestDir "pass\stress\$sub"
        if (-not (Test-Path $dir)) { continue }
        if (-not $printed) { Write-Host ''; Write-Host 'STRESS tests:'; $printed = $true }
        foreach ($f in Get-ChildItem -Path $dir -Filter *.gray -File) {
            Test-ShouldPass "stress/$sub/$($f.BaseName)" @($f.FullName)
        }
    }
}

if (Should-Run 'warnings') {
    $dir = Join-Path $TestDir 'pass\warnings'
    if (Test-Path $dir) {
        Write-Host ''
        Write-Host 'WARNING tests:'
        foreach ($f in Get-ChildItem -Path $dir -Filter *.gray -File) {
            $label = "warnings/$($f.BaseName)"
            if ($f.BaseName -notmatch '^(W\d+)') {
                Write-Fail $label '(filename does not name a W-code)'
                continue
            }
            $expected = $Matches[1]
            $r = Invoke-GrayTest -Arguments @('check', $f.FullName)
            if ($r.Output -match "warning\[$expected\]") {
                Write-Pass $label
            } else {
                Write-Fail $label "(expected $expected)" $r.Output
            }
        }
    }
}

# ---- fail/ ----------------------------------------------------------------

Write-Host ''
Write-Host 'FAIL tests (expecting errors):'

if (Should-Run 'errors') {
    $dir = Join-Path $TestDir 'fail\errors'
    if (Test-Path $dir) {
        foreach ($f in Get-ChildItem -Path $dir -Filter *.gray -File) {
            Test-ShouldFail "errors/$($f.BaseName)" @($f.FullName)
        }
    }
}

# Programs under test may leave a database behind.
Get-ChildItem -Path $GrayRepoRoot -Filter *.graydb -File -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue

if (Should-Run 'multi-file') {
    $dir = Join-Path $TestDir 'fail\multi-file'
    if (Test-Path $dir) {
        foreach ($f in Get-ChildItem -Path $dir -Filter *.gray -File) {
            Test-ShouldFail "multi-file/$($f.BaseName)" @($f.FullName)
        }
        foreach ($d in Get-ChildItem -Path $dir -Directory) {
            $main = Get-MainFile $d.FullName
            if ($main) { Test-ShouldFail "multi-file/$($d.Name)" @($main) }
        }
    }
}

# ---- Summary --------------------------------------------------------------

$total = $script:PassCount + $script:FailCount
Write-Host ''
Write-Host '========================================'
Write-Host '  Test Summary'
Write-Host '========================================'
Write-Host ''
Write-Host "  Passed:  $($script:PassCount)" -ForegroundColor Green
Write-Host "  Failed:  $($script:FailCount)" -ForegroundColor Red
Write-Host "  Total:   $total"
Write-Host ''

if ($script:FailCount -eq 0) {
    Write-Host '  ALL TESTS PASSED!' -ForegroundColor Green
    exit 0
}

Write-Host '  SOME TESTS FAILED' -ForegroundColor Red
exit 1
