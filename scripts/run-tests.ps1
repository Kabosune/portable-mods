<#
.SYNOPSIS
    Compiles and runs all pure-C11 core unit tests in portable-mods.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$TempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("portable-mods-tests-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

function Find-CCompiler {
    foreach ($name in @('clang', 'gcc')) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) {
            return @{ Kind = 'gnu'; Path = $cmd.Source }
        }
    }

    $msysCandidates = @(
        'C:\msys64\mingw64\bin\gcc.exe',
        'C:\msys64\ucrt64\bin\gcc.exe',
        'C:\msys64\clang64\bin\clang.exe',
        'C:\Program Files\LLVM\bin\clang.exe'
    )
    foreach ($candidate in $msysCandidates) {
        if (Test-Path $candidate) {
            return @{ Kind = 'gnu'; Path = $candidate }
        }
    }

    $zig = Get-Command 'zig' -ErrorAction SilentlyContinue
    if ($zig) {
        return @{ Kind = 'zig'; Path = $zig.Source }
    }

    $cl = Get-Command 'cl' -ErrorAction SilentlyContinue
    if ($cl) {
        return @{ Kind = 'msvc'; Path = $cl.Source }
    }

    return $null
}

$compiler = Find-CCompiler
if (-not $compiler) {
    Write-Error "No C11 compiler found (checked clang, gcc, MSYS2 MinGW64/UCRT64, zig, cl)."
    exit 1
}

Write-Host "Using compiler: $($compiler.Path) ($($compiler.Kind))"
$compilerDir = Split-Path -Parent $compiler.Path
if ($compilerDir -and ($env:PATH -notlike "*$compilerDir*")) {
    $env:PATH = "$compilerDir;$env:PATH"
}

$tests = @(
    @{
        Name    = 'test_qm_physics'
        Sources = @(
            (Join-Path $RepoRoot 'mods\quake-movement\core\qm_physics.c'),
            (Join-Path $RepoRoot 'mods\quake-movement\core\test_qm_physics.c')
        )
        Include = (Join-Path $RepoRoot 'mods\quake-movement\core')
    },
    @{
        Name    = 'test_weighty_aim'
        Sources = @(
            (Join-Path $RepoRoot 'mods\weighty-aim\core\weighty_aim.c'),
            (Join-Path $RepoRoot 'mods\weighty-aim\core\test_weighty_aim.c')
        )
        Include = (Join-Path $RepoRoot 'mods\weighty-aim\core')
    }
)

$exitCode = 0
try {
    foreach ($t in $tests) {
        $exePath = Join-Path $TempDir ($t.Name + '.exe')
        Write-Host "`n=== Building $($t.Name) ==="

        if ($compiler.Kind -eq 'gnu') {
            $args = @('-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', "-I$($t.Include)") + $t.Sources + @('-lm', '-o', $exePath)
            & $compiler.Path @args
        } elseif ($compiler.Kind -eq 'zig') {
            $args = @('cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', "-I$($t.Include)") + $t.Sources + @('-lm', '-o', $exePath)
            & $compiler.Path @args
        } else {
            $objDir = (Join-Path $TempDir $t.Name) + '\'
            New-Item -ItemType Directory -Path $objDir -Force | Out-Null
            $args = @('/nologo', '/std:c11', '/O2', '/W4', '/WX', "/I$($t.Include)", "/Fo$objDir", "/Fe$exePath") + $t.Sources
            & $compiler.Path @args
        }

        if ($LASTEXITCODE -ne 0) {
            throw "Compilation failed for $($t.Name) (exit code $LASTEXITCODE)"
        }

        Write-Host "=== Running $($t.Name) ==="
        & $exePath
        if ($LASTEXITCODE -ne 0) {
            throw "Test failed for $($t.Name) (exit code $LASTEXITCODE)"
        }
    }

    Write-Host "`nAll pure-C11 core test suites passed!"
} catch {
    Write-Error $_
    $exitCode = 1
} finally {
    if (Test-Path $TempDir) {
        Remove-Item -Recurse -Force $TempDir -ErrorAction SilentlyContinue
    }
}

exit $exitCode
