$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $PSScriptRoot 'autobuild_regression.cpp'
$solver = Join-Path $root 'src\features\autobuild\services\Solver.cpp'
$rules = Join-Path $root 'src\features\autobuild\services\RuleInference.cpp'
$smart = Join-Path $root 'src\features\autobuild\services\SmartTemplateEngine.cpp'
$output = Join-Path $env:TEMP 'paimon-autobuild-regression.exe'
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

& $compiler /nologo /std:c++latest /EHsc /W4 /WX $source $solver $rules $smart "/Fe:$output"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $output
exit $LASTEXITCODE
