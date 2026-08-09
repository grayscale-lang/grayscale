<#
    test.ps1 - native Windows test runner for the Grayscale compiler frontend.

    Runs the four unit suites that do not depend on the Grayscale runtime or
    standard library. The remaining suites need POSIX facilities or
    libgrayrt.a and are reported as skipped rather than silently omitted.

    Usage (from the repo root):
      .\scripts\test.ps1
      .\scripts\test.ps1 -KeepBinaries      # leave the test .exe files in place
      .\scripts\test.ps1 -CC clang

    Author:  Aristomedes (@Aristomedes)
    Copyright (c) 2025-Present Marshall A Burns
    Licensed under the MIT License. See LICENSE for details.
#>

[CmdletBinding()]
param(
    [string]$CC = '',
    [switch]$KeepBinaries,
    [switch]$SkipGo
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$TestsDir = Join-Path $GraycDir 'tests'

# Suites that build against the compiler frontend only.
$Suites = @('lexer', 'parser', 'typechecker', 'util')

# Suites deferred until the runtime and standard library are ported.
$Skipped = [ordered]@{
    'codegen' = 'uses popen() and /tmp'
    'panics'  = 'uses fork()'
    'runtime' = 'requires libgrayrt.a'
    'stdlib'  = 'requires libgrayrt.a'
}

# One shared dependency list for every suite. test_util needs fewer objects,
# but linking the superset is harmless and removes a list that can drift.
# platform.c is mandatory: error.c calls gray_stderr_is_tty().
$Deps = @(
    'util\arena.c', 'util\buf.c', 'util\error.c', 'util\error_codes.c', 'util\platform.c',
    'lexer\token.c', 'lexer\lexer.c',
    'parser\ast.c', 'parser\parser.c',
    'typechecker\types.c', 'typechecker\scope.c', 'typechecker\typechecker.c'
) | ForEach-Object { Join-Path $GraycSrcDir $_ }

$cc  = Resolve-GrayCC -Preferred $CC
$rsp = New-GrayResponseFile -Path (Join-Path $GraycDir 'test.rsp') `
    -Flags (Get-GrayCFlags)

$built  = @()
$failed = @()

foreach ($suite in $Suites) {
    $source = Join-Path $TestsDir "test_$suite.c"
    $exe    = Join-Path $TestsDir "test_$suite.exe"
    if (-not (Test-Path $source)) {
        Write-Host "SKIP  test_$suite (no $source)" -ForegroundColor Yellow
        continue
    }

    Write-Host "=== Building test_$suite ==="
    Invoke-GrayNative { & $cc "@$rsp" $source @Deps -o $exe }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "BUILD FAILED test_$suite" -ForegroundColor Red
        $failed += $suite
        continue
    }
    $built += $exe

    Write-Host "=== Running test_$suite ==="
    Invoke-GrayNative { & $exe }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED test_$suite (exit $LASTEXITCODE)" -ForegroundColor Red
        $failed += $suite
    }
}

Write-Host ''
foreach ($suite in $Skipped.Keys) {
    Write-Host ("SKIPPED test_{0,-10} - {1} (Windows runtime port pending)" -f $suite, $Skipped[$suite]) `
        -ForegroundColor DarkGray
}
Write-Host 'SKIPPED integration suite - scripts/run_tests.sh needs bash' -ForegroundColor DarkGray

if (-not $SkipGo) {
    Write-Host ''
    Write-Host '=== Go Unit Tests ==='
    New-GrayEmbedStubs
    $go = Resolve-GrayGo
    Push-Location $GrayRepoRoot
    try {
        Invoke-GrayNative { & $go test -count=1 ./cli/... ./internal/driver/... }
        if ($LASTEXITCODE -ne 0) { $failed += 'go' }
    } finally {
        Pop-Location
    }
}

if (-not $KeepBinaries) {
    foreach ($exe in $built) { Remove-Item -Force $exe -ErrorAction SilentlyContinue }
}

Write-Host ''
if ($failed.Count -gt 0) {
    Write-Host ("FAILED: " + ($failed -join ', ')) -ForegroundColor Red
    exit 1
}
Write-Host 'All Windows test suites passed.' -ForegroundColor Green
