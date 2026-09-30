$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $PSScriptRoot 'editor_physics_preview_regression.cpp'
$output = Join-Path $env:TEMP 'paimon-editor-physics-preview-regression.exe'
$cache = Join-Path $root 'build\CMakeCache.txt'

# the three units keep their own anonymous namespaces, so they are compiled
# side by side instead of being included into the test the way the other banks
# do it.
$units = @(
    (Join-Path $root 'src\features\editor-physics\services\NativePreview.cpp'),
    (Join-Path $root 'src\features\editor-physics\services\PhysicsSolver.cpp'),
    (Join-Path $root 'src\features\editor-physics\PhysicsNative.cpp')
)

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

& $compiler /nologo /std:c++latest /EHsc $source @units "/Fe:$output"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $output
exit $LASTEXITCODE
