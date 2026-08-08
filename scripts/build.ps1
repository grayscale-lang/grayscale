<#
    build.ps1 - native Windows build for the Grayscale compiler and CLI.

    Windows contributors do not need MSYS2, make, or a POSIX shell; this script
    drives MinGW-w64 GCC (or Clang) directly.

    What it builds:
      grayc.exe  the Grayscale compiler frontend
      gray.exe   the Go CLI wrapper

    What it does NOT build: libgrayrt.a, the Grayscale runtime and standard
    library. Those are still POSIX-only, so `gray build` and `gray run` cannot
    produce a binary on Windows yet. `check`, `fmt`, `doc`, and
    `build --emit-c` all work.

    Usage (from the repo root):
      .\scripts\build.ps1                    # compiler + CLI
      .\scripts\build.ps1 -Target compiler   # just grayc.exe
      .\scripts\build.ps1 -DebugBuild        # -g -O0 -DDEBUG
      .\scripts\build.ps1 -CC clang          # pick a compiler explicitly
      .\scripts\build.ps1 -Target clean

    Author:  Aristomedes (@Aristomedes)
    Copyright (c) 2025-Present Marshall A Burns
    Licensed under the MIT License. See LICENSE for details.
#>

[CmdletBinding()]
param(
    [ValidateSet('all', 'compiler', 'cli', 'stubs', 'clean')]
    [string]$Target = 'all',
    [switch]$DebugBuild,
    [string]$CC = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$GraycExe = Join-Path $GraycDir 'grayc.exe'
$GrayExe  = Join-Path $GrayRepoRoot 'gray.exe'

function Invoke-Clean {
    Write-Host 'Cleaning build artifacts...'
    $paths = @(
        $GraycExe,
        $GrayExe,
        (Join-Path $GraycDir 'build.rsp'),
        (Join-Path $GraycDir 'libgrayrt.a'),
        (Join-Path $GrayRepoRoot 'dist'),
        $GrayEmbedDir
    )
    foreach ($path in $paths) {
        if (Test-Path $path) { Remove-Item -Recurse -Force $path }
    }
    Get-ChildItem -Path $GraycDir -Recurse -Include *.o, *.d, test_*.exe -ErrorAction SilentlyContinue |
        Remove-Item -Force
    Write-Host 'Clean complete'
}

function Invoke-BuildCompiler {
    $cc      = Resolve-GrayCC -Preferred $CC
    $version = Get-GrayVersion
    $sources = @(Get-GraycSources)

    Write-Host "Building compiler with $cc ($version)..."
    Write-Host "  $($sources.Count) source files"

    $flags = Get-GrayCFlags -DebugBuild:$DebugBuild -Pedantic -Version $version
    $rsp   = New-GrayResponseFile -Path (Join-Path $GraycDir 'build.rsp') -Flags $flags

    Invoke-GrayCC -CC $cc -ResponseFile $rsp -What 'Compiler build' `
        -Arguments ($sources + @('-o', $GraycExe, '-lm'))

    Write-Host "  -> $GraycExe" -ForegroundColor Green
    Write-Host ''
    Write-Host 'Skipping libgrayrt.a - the Windows runtime/stdlib port is not done yet,' -ForegroundColor Yellow
    Write-Host 'so this build cannot link Grayscale programs into binaries.' -ForegroundColor Yellow
}

function Invoke-StageEmbedAssets {
    # go:embed reads the literal path internal/driver/runtime/grayc, with no
    # extension, on every platform. The .exe rename happens on extraction
    # (see internal/driver/embedded.go).
    New-GrayEmbedStubs
    if (Test-Path $GraycExe) {
        Copy-Item $GraycExe (Join-Path $GrayEmbedDir 'grayc') -Force
    }
    foreach ($tree in @('runtime', 'stdlib')) {
        $src = Join-Path $GraycSrcDir $tree
        $dst = Join-Path $GrayEmbedDir "src\$tree"
        if (-not (Test-Path $dst)) { New-Item -ItemType Directory -Force -Path $dst | Out-Null }
        Get-ChildItem -Path $src -Include *.h, *.c -File | Copy-Item -Destination $dst -Force
    }
    # libgrayrt.a stays a zero-length stub until the runtime port lands.
}

function Invoke-BuildCli {
    $go = Resolve-GrayGo

    Invoke-StageEmbedAssets

    $version   = Get-GrayVersion
    $buildTime = (Get-Date -Format 'yyyy-MM-dd_HH:mm:ss')
    $ldflags   = "-X main.Version=$version -X main.BuildTime=$buildTime"

    Write-Host 'Building gray CLI...'
    Push-Location $GrayRepoRoot
    try {
        Invoke-GrayNative { & $go build -ldflags $ldflags -o $GrayExe ./cli }
        if ($LASTEXITCODE -ne 0) { throw "go build failed (exit $LASTEXITCODE)" }
    } finally {
        Pop-Location
    }
    Write-Host "  -> $GrayExe" -ForegroundColor Green
}

switch ($Target) {
    'clean'    { Invoke-Clean }
    'stubs'    { New-GrayEmbedStubs; Write-Host 'Embed stubs created' }
    'compiler' { Invoke-BuildCompiler }
    'cli'      { Invoke-BuildCli }
    'all'      {
        Invoke-BuildCompiler
        Invoke-BuildCli
        Write-Host ''
        Write-Host 'Build complete.' -ForegroundColor Green
        Write-Host '  .\gray.exe check <file.gray>'
        Write-Host '  .\gray.exe fmt <path>'
        Write-Host '  .\gray.exe build <file.gray> --emit-c'
    }
}
