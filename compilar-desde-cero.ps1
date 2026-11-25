#requires -Version 5.1
<#
Ejecutar: doble clic en compilar-desde-cero.cmd, o .\compilar-desde-cero.cmd
Solo verificar herramientas: agregar -Comprobar
Por defecto usa el doble de tareas que procesadores logicos. Ajustar con -Hilos 12, 24, 36, etc.
Cada ejecucion crea build-benchmark/<fecha>; no elimina compilaciones anteriores.
El paquete terminado se copia a C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash\geode\mods.
#>
[CmdletBinding()]
param(
    [ValidateRange(1, 1024)]
    [int]$Hilos = ([Environment]::ProcessorCount * 2),
    [switch]$Comprobar,
    [switch]$SinAdministradorDeTareas
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-CMakeLogged {
    param([string[]]$Arguments, [string]$LogPath)
    # Windows PowerShell 5.1 convierte stderr en registros de error.
    $ErrorActionPreference = 'Continue'
    & cmake @Arguments 2>&1 | Tee-Object -FilePath $LogPath
    if ($LASTEXITCODE -ne 0) {
        throw "CMake fallo (codigo $LASTEXITCODE). Revisa $LogPath"
    }
}

function Initialize-CompilerEnvironment {
    if ($env:VSCMD_ARG_TGT_ARCH -eq 'x64') { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'No se encontro Visual Studio Installer con las herramientas de C++.'
    }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'Faltan las herramientas de C++ de Visual Studio.' }
    & (Join-Path $installation 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') { throw 'No se pudo activar el entorno de C++ x64.' }
}

Initialize-CompilerEnvironment
foreach ($tool in @('cmake', 'ninja', 'clang-cl', 'lld-link', 'geode', 'git')) {
    $null = Get-Command $tool -ErrorAction Stop
}
if (-not $env:GEODE_SDK -or -not (Test-Path -LiteralPath (Join-Path $env:GEODE_SDK 'CMakeLists.txt'))) {
    throw 'Configura GEODE_SDK con la ruta al SDK de Geode.'
}
$computer = Get-CimInstance Win32_ComputerSystem
Write-Host ('Tareas simultaneas: {0} | Procesadores logicos: {1} | RAM instalada: {2:N1} GiB' -f $Hilos, $computer.NumberOfLogicalProcessors, ($computer.TotalPhysicalMemory / 1GB))
Write-Host "SDK: $env:GEODE_SDK"
if ($Comprobar) {
    Write-Host 'Herramientas disponibles. No se inicio la compilacion.'
    return
}

$runDirectory = Join-Path $PSScriptRoot ('build-benchmark\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$null = New-Item -ItemType Directory -Path $runDirectory
$samplesPath = Join-Path $runDirectory 'rendimiento.csv'
$stopPath = Join-Path $runDirectory 'detener-monitor'
$configureLog = Join-Path $runDirectory 'configurar.log'
$buildLog = Join-Path $runDirectory 'compilar.log'

$configureArguments = @(
    '-S', $PSScriptRoot, '-B', $runDirectory, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    '-DCMAKE_C_COMPILER=clang-cl', '-DCMAKE_CXX_COMPILER=clang-cl',
    '-DCMAKE_LINKER=lld-link',
    '-DCMAKE_C_COMPILER_LAUNCHER=', '-DCMAKE_CXX_COMPILER_LAUNCHER=',
    '-DGEODE_DONT_USE_CCACHE=ON', '-DGEODE_DONT_INSTALL_MODS=ON',
    '-DPAIMBNAILS_EXTRA_WARNINGS=ON',
    '-DPAIMBNAILS_DEPLOY_LOCAL_MOD=ON',
    # El Codegen precompilado de GitHub (latest) genera headers estilo
    # Geode/mapping/* que el loader 6.0.0 no trae: compilarlo en local
    # desde el checkout fijado en PAIMBNAILS_BINDINGS_REVISION.
    '-DSKIP_BUILDING_CODEGEN=OFF'
)
# Reutilizar las descargas no reutiliza objetos ni resultados de compilacion.
$downloadCache = Join-Path $PSScriptRoot 'build\cpm-cache'
if (Test-Path -LiteralPath $downloadCache) {
    $configureArguments += "-DCPM_SOURCE_CACHE=$($downloadCache.Replace('\', '/'))"
}

Write-Host "Configurando desde cero en: $runDirectory"
Invoke-CMakeLogged -Arguments $configureArguments -LogPath $configureLog

if (-not $SinAdministradorDeTareas) {
    Start-Process taskmgr.exe
}

# Las muestras corresponden a TODO el equipo durante la fase de compilacion.
# CIM evita depender del idioma de los nombres de contadores de Windows.
$monitor = Start-Job -ArgumentList $samplesPath, $stopPath -ScriptBlock {
    param($csvPath, $stopFile)
    $ErrorActionPreference = 'Stop'
    while (-not (Test-Path -LiteralPath $stopFile)) {
        $processor = Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'"
        $memory = Get-CimInstance Win32_OperatingSystem
        if ($null -eq $processor) { throw 'No se pudo consultar el contador de CPU.' }
        [pscustomobject]@{
            Fecha = (Get-Date).ToString('o')
            CPU_Porcentaje = $processor.PercentProcessorTime
            RAM_Usada_GiB = (($memory.TotalVisibleMemorySize - $memory.FreePhysicalMemory) / 1MB).ToString('F3', [Globalization.CultureInfo]::InvariantCulture)
            RAM_Disponible_GiB = ($memory.FreePhysicalMemory / 1MB).ToString('F3', [Globalization.CultureInfo]::InvariantCulture)
        } | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Append -Encoding UTF8
        Start-Sleep -Seconds 1
    }
}

$timer = [Diagnostics.Stopwatch]::StartNew()
try {
    Write-Host "Compilando Release con $Hilos tareas simultaneas. Mira Rendimiento en el Administrador de tareas."
    Invoke-CMakeLogged -Arguments @('--build', $runDirectory, '--target', 'Paimbnails_PACKAGE', '--parallel', "$Hilos") -LogPath $buildLog
}
finally {
    $timer.Stop()
    $null = New-Item -ItemType File -Path $stopPath -Force
    $null = Wait-Job $monitor -Timeout 10
    if ($monitor.State -ne 'Completed') {
        Write-Warning "Monitor: $($monitor.State). $($monitor.ChildJobs[0].JobStateInfo.Reason)"
        Stop-Job $monitor
    }
    Receive-Job $monitor -ErrorAction Continue
    Remove-Job $monitor
    Write-Host ('Tiempo de compilacion: {0:N1} segundos' -f $timer.Elapsed.TotalSeconds)
    if (Test-Path -LiteralPath $samplesPath) {
        $samples = Import-Csv -LiteralPath $samplesPath
        $cpuUsage = $samples | Measure-Object CPU_Porcentaje -Average -Maximum
        $ramUsage = $samples | Measure-Object RAM_Usada_GiB -Maximum
        Write-Host ('CPU total media: {0:N1}% | Pico: {1:N1}% | Pico RAM total usada: {2:N2} GiB' -f $cpuUsage.Average, $cpuUsage.Maximum, $ramUsage.Maximum)
        Write-Host "Muestras del equipo (~1 segundo): $samplesPath"
    }
    else { Write-Warning 'No se obtuvieron muestras de rendimiento.' }
    Write-Host "Log de compilacion: $buildLog"
}
Write-Host "Compilacion terminada. Archivos generados en: $runDirectory"
