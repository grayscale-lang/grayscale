<#
    common.ps1 - shared helpers for build.ps1 and test.ps1.

    Dot-source this file; on its own it does nothing.

    Author:  Aristomedes (@Aristomedes)
    Copyright (c) 2025-Present Marshall A Burns
    Licensed under the MIT License. See LICENSE for details.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# The repo root is the parent of the scripts/ directory holding this file.
$GrayRepoRoot = Split-Path -Parent $PSScriptRoot
$GraycDir     = Join-Path $GrayRepoRoot 'grayc'
$GraycSrcDir  = Join-Path $GraycDir 'src'
$GrayEmbedDir = Join-Path $GrayRepoRoot 'internal\driver\runtime'

function Register-GrayCC {
    <#
        .SYNOPSIS
        Put the compiler's own directory on PATH for this session, then return
        the compiler path unchanged.

        .DESCRIPTION
        gcc.exe runs fine on its own, but the backend it spawns (cc1.exe) links
        against libisl/libmpc/libmpfr/libgmp, which live next to gcc.exe. If
        that directory is not on PATH, cc1 fails to load and the build dies with
        exit 1 and *no diagnostic at all* - a silent failure that is very hard to
        recognise. MSYS2 and most MinGW distributions deliberately do not add
        themselves to PATH, so this is the common case, not an edge case.
    #>
    param([string]$CCPath)

    $binDir = Split-Path -Parent $CCPath
    if ($binDir -and ($env:PATH -split ';') -notcontains $binDir) {
        $env:PATH = "$binDir;$env:PATH"
    }
    return $CCPath
}

function Resolve-GrayCC {
    <#
        .SYNOPSIS
        Locate a usable C compiler, preferring an explicit choice over $env:CC
        over whatever is on PATH.
    #>
    param([string]$Preferred = '')

    $candidates = @()
    if ($Preferred) { $candidates += $Preferred }
    if ($env:CC)    { $candidates += $env:CC }
    $candidates += @('gcc', 'clang')

    foreach ($candidate in $candidates) {
        $found = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($found) { return (Register-GrayCC $found.Source) }
    }

    # Not on PATH. MSYS2 and the common MinGW distributions deliberately do not
    # add themselves, so a working toolchain being "missing" is the normal case
    # rather than the exception - look where they actually install.
    $wellKnown = @(
        'C:\msys64\ucrt64\bin\gcc.exe',
        'C:\msys64\mingw64\bin\gcc.exe',
        'C:\msys64\clang64\bin\clang.exe',
        'C:\mingw64\bin\gcc.exe',
        'C:\ProgramData\mingw64\mingw64\bin\gcc.exe',
        "$env:ProgramFiles\LLVM\bin\clang.exe"
    )
    foreach ($path in $wellKnown) {
        if (Test-Path $path) {
            Write-Host "Using C compiler at $path" -ForegroundColor DarkGray
            Write-Host "  (not on PATH; add $(Split-Path -Parent $path) to PATH to silence this)" `
                -ForegroundColor DarkGray
            return (Register-GrayCC $path)
        }
    }

    Write-Host ''
    Write-Host 'No C compiler found.' -ForegroundColor Red
    Write-Host 'Building Grayscale on Windows needs MinGW-w64 GCC or Clang. Install one of:'
    Write-Host ''
    Write-Host '    winget install BrechtSanders.WinLibs.POSIX.UCRT.Base'
    Write-Host '    choco install mingw'
    Write-Host '    (or install MSYS2 and add C:\msys64\ucrt64\bin to PATH)'
    Write-Host ''
    Write-Host 'Already installed? Pass it explicitly:  .\scripts\build.ps1 -CC C:\path\to\gcc.exe'
    Write-Host ''
    exit 1
}

function Resolve-GrayGo {
    <#
        .SYNOPSIS
        Locate the Go toolchain, falling back to its default install locations.

        .DESCRIPTION
        The Go installer edits the machine PATH, so a shell opened before the
        install (or a CI step with a trimmed environment) will not see it.
        Checking the default locations avoids a confusing "Go not found" on a
        machine that plainly has Go.
    #>
    $found = Get-Command go -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }

    $wellKnown = @(
        "$env:ProgramFiles\Go\bin\go.exe",
        "$env:LOCALAPPDATA\Programs\Go\bin\go.exe",
        'C:\Go\bin\go.exe'
    )
    foreach ($path in $wellKnown) {
        if (Test-Path $path) {
            $binDir = Split-Path -Parent $path
            if (($env:PATH -split ';') -notcontains $binDir) { $env:PATH = "$binDir;$env:PATH" }
            return $path
        }
    }

    Write-Host ''
    Write-Host 'Go not found.' -ForegroundColor Red
    Write-Host 'The gray CLI needs Go 1.23+. Install it from https://go.dev/dl/'
    Write-Host ''
    exit 1
}

function Get-GrayVersion {
    $ErrorActionPreference = 'Continue'
    $version = & git -C $GrayRepoRoot describe --tags --always --dirty 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $version) { return 'unknown' }
    return ([string]($version | Select-Object -First 1)).Trim()
}

function Get-GraycSources {
    <#
        .SYNOPSIS
        The compiler frontend: every .c under grayc/src outside the runtime,
        stdlib, and vendor trees.

        .DESCRIPTION
        Those three trees are compiled into libgrayrt.a and linked into user
        programs, not into grayc itself - which is exactly why the compiler can
        be built on Windows before the runtime has been ported. Globbed rather
        than listed so this cannot drift out of sync with grayc/Makefile.
    #>
    Get-ChildItem -Path $GraycSrcDir -Recurse -Filter *.c |
        Where-Object { $_.FullName -notmatch '\\(runtime|stdlib|vendor)\\' } |
        ForEach-Object { $_.FullName } |
        Sort-Object
}

function Get-GrayCFlags {
    param(
        [switch]$DebugBuild,
        [switch]$Pedantic,
        [string]$Version = ''
    )

    $flags = @(
        # gnu11 rather than c11: -std=c11 defines __STRICT_ANSI__ on MinGW-w64,
        # which unbinds printf from the ANSI-conforming implementation. %zu and
        # %lld would then be emitted verbatim into generated C and into
        # diagnostics, and the POSIX-shaped names in <io.h> would be hidden.
        '-std=gnu11',
        '-D__USE_MINGW_ANSI_STDIO=1',
        '-D_WIN32_WINNT=0x0601',
        '-Wall',
        '-Wextra',
        '-Wno-unused-parameter'
    )
    if ($Pedantic)   { $flags += '-Wpedantic' }
    if ($DebugBuild) { $flags += @('-g', '-O0', '-DDEBUG') }
    else             { $flags += '-O2' }
    if ($Version) {
        # GCC's response-file parser strips bare quotes, so the quotes that must
        # survive into the macro have to be backslash-escaped.
        $flags += ('-DGRAY_VERSION=' + '\"' + $Version + '\"')
    }
    $flags += "-I$GraycSrcDir"
    return $flags
}

function New-GrayResponseFile {
    <#
        .SYNOPSIS
        Write compiler flags to a GCC response file and return its path.

        .DESCRIPTION
        Windows PowerShell mangles embedded quotes when forwarding arguments to
        a native executable, and -DGRAY_VERSION=\"...\" is precisely that shape.
        A response file sidesteps PowerShell's quoting entirely and has no
        command-line length limit.
    #>
    param([string]$Path, [string[]]$Flags)

    Set-Content -Path $Path -Value $Flags -Encoding ascii
    return $Path
}

function Invoke-GrayNative {
    <#
        .SYNOPSIS
        Run a native command, letting its output through and leaving the result
        in the automatic $LASTEXITCODE for the caller to check.

        .DESCRIPTION
        Windows PowerShell 5.1 wraps every line a native command writes to
        stderr in an ErrorRecord. Under ErrorActionPreference=Stop that means a
        single compiler *warning* aborts the build. Exit code is the only
        trustworthy success signal, so drop to Continue for the duration of the
        call; ErrorActionPreference is function-scoped, so it reverts on return.

        This deliberately returns nothing. Returning the exit code would splice
        it onto the end of the command's own stdout, and the caller would be
        comparing an array against 0.
    #>
    param([scriptblock]$Command)

    $ErrorActionPreference = 'Continue'
    & $Command
}

function Invoke-GrayCC {
    <#
        .SYNOPSIS
        Run the C compiler, throwing with context if it fails.
    #>
    param(
        [string]$CC,
        [string]$ResponseFile,
        [string[]]$Arguments,
        [string]$What = 'compile'
    )

    Invoke-GrayNative { & $CC "@$ResponseFile" @Arguments }
    if ($LASTEXITCODE -ne 0) {
        throw "$What failed (exit $LASTEXITCODE)"
    }
}

function New-GrayEmbedStubs {
    <#
        .SYNOPSIS
        Create the zero-length embed stubs go:embed requires at build time.

        .DESCRIPTION
        Mirrors the `stubs` target in the root Makefile. The go:embed directives
        in internal/driver/embedded.go fail the Go build outright if these paths
        do not exist. The extractor recognises an empty grayc stub and falls
        back to a path search, so dev builds still work.
    #>
    foreach ($dir in @("$GrayEmbedDir\src\runtime", "$GrayEmbedDir\src\stdlib")) {
        if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    }
    $stubs = @(
        "$GrayEmbedDir\grayc",
        "$GrayEmbedDir\libgrayrt.a",
        "$GrayEmbedDir\src\runtime\.stub",
        "$GrayEmbedDir\src\stdlib\.stub"
    )
    foreach ($stub in $stubs) {
        # Never -Force an existing file here; that would truncate a real
        # staged artifact.
        if (-not (Test-Path $stub)) { New-Item -ItemType File -Path $stub | Out-Null }
    }
}
