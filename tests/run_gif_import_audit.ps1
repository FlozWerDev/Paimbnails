param(
    [Parameter(Mandatory = $true)][string] $Images,
    [string] $Mode = 'paint',
    [int] $Dimension = 64,
    [int] $Colors = 16,
    [int] $Top = 0,
    [string] $Dump = ''
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $PSScriptRoot 'gif_import_audit.cpp'
$output = Join-Path $env:TEMP 'paimon-gif-import-audit.exe'
$cache = Join-Path $root 'build\CMakeCache.txt'

$compiler = if (Test-Path -LiteralPath $cache) {
    $entry = Select-String -LiteralPath $cache -Pattern '^CMAKE_CXX_COMPILER:STRING=' | Select-Object -First 1
    if ($entry) { $entry.Line.Split('=', 2)[1] }
}
if (-not $compiler) {
    $compiler = (Get-Command clang-cl -ErrorAction SilentlyContinue).Source
}
if (-not $compiler -or -not (Test-Path -LiteralPath $compiler)) {
    throw 'clang-cl not found; configure the main build first'
}

& $compiler /nologo /std:c++latest /EHsc /O2 $source "/Fe:$output"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$arguments = @($Images, '--mode', $Mode, '--dim', $Dimension, '--colors', $Colors)
if ($Top -gt 0) { $arguments += @('--top', $Top) }
if ($Dump) { $arguments += @('--dump', $Dump) }

& $output @arguments
exit $LASTEXITCODE
