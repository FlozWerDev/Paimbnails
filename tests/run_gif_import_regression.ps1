$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $PSScriptRoot 'gif_import_regression.cpp'
$output = Join-Path $env:TEMP 'paimon-gif-import-regression.exe'
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

& $compiler /nologo /std:c++latest /EHsc $source "/Fe:$output"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $output
exit $LASTEXITCODE
