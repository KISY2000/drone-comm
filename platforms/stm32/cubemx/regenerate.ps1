param(
    [string]$CubeMXRoot = 'C:\Users\Li\AppData\Local\Programs\STM32CubeMX',
    [string]$FirmwareRoot = '',
    [string]$OutputRoot = '',
    [switch]$FetchOfficialFirmware
)
$ErrorActionPreference = 'Stop'
$sourceRoot = $PSScriptRoot
$projectRoot = (Resolve-Path (Join-Path $sourceRoot '..\..\..')).Path
if (!$FirmwareRoot) { $FirmwareRoot = Join-Path $projectRoot 'build\cubemx_probe\firmware' }
if (!$OutputRoot) { $OutputRoot = Join-Path $projectRoot 'build\cubemx_regenerated' }
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw ('Output already exists; choose a fresh -OutputRoot: ' + $OutputRoot) }
$java = Join-Path $CubeMXRoot 'jre\bin\java.exe'
$cube = Join-Path $CubeMXRoot 'STM32CubeMX.exe'
if (!(Test-Path -LiteralPath $java) -or !(Test-Path -LiteralPath $cube)) { throw 'CubeMX installation missing' }
$items = @(
    @{Role='ground_f103'; Family='STM32CubeF1'; Tag='v1.8.7'; Device='STM32F1xx'; Pack='Keil::STM32F1xx_DFP@2.4.1'},
    @{Role='ground_f407'; Family='STM32CubeF4'; Tag='v1.28.3'; Device='STM32F4xx'; Pack='Keil::STM32F4xx_DFP@3.1.1'}
)
foreach ($item in $items) {
    $firmware = Join-Path $FirmwareRoot $item.Family
    if ($FetchOfficialFirmware -and !(Test-Path -LiteralPath $firmware)) {
        New-Item -ItemType Directory -Force -Path $FirmwareRoot | Out-Null
        & git clone --depth 1 --filter=blob:none --sparse --branch $item.Tag ('https://github.com/STMicroelectronics/' + $item.Family + '.git') $firmware
        if ($LASTEXITCODE) { throw 'Official firmware clone failed' }
        & git -C $firmware sparse-checkout set Drivers
        if ($LASTEXITCODE) { throw 'Official driver checkout failed' }
        & git -C $firmware submodule update --init --depth 1 ('Drivers/CMSIS/Device/ST/' + $item.Device) ('Drivers/' + $item.Device + '_HAL_Driver')
        if ($LASTEXITCODE) { throw 'Official pinned submodule checkout failed' }
    }
    if (!(Test-Path -LiteralPath (Join-Path $firmware 'package.xml'))) { throw ('Firmware missing. Run with -FetchOfficialFirmware or pass -FirmwareRoot: ' + $firmware) }
}
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$logRoot = Join-Path $OutputRoot 'logs'
New-Item -ItemType Directory -Path $logRoot | Out-Null
foreach ($item in $items) {
    $firmware = [System.IO.Path]::GetFullPath((Join-Path $FirmwareRoot $item.Family))
    $roleRoot = Join-Path $OutputRoot $item.Role
    New-Item -ItemType Directory -Path $roleRoot | Out-Null
    $ioc = Join-Path $roleRoot ($item.Role + '.ioc')
    $sourceIoc = Join-Path (Join-Path $sourceRoot $item.Role) ($item.Role + '.ioc')
    $iocText = Get-Content -LiteralPath $sourceIoc -Raw
    # Set the valid firmware path before config load, avoiding missing-package prompts.
    $iocText = [regex]::Replace($iocText, '(?m)^ProjectManager.CustomerFirmwarePackage=.*$', ('ProjectManager.CustomerFirmwarePackage=' + $firmware.Replace('\','/')))
    [System.IO.File]::WriteAllText($ioc, $iocText, (New-Object System.Text.UTF8Encoding($false)))
    $script = Join-Path $logRoot ($item.Role + '.script')
    $scriptLines = @(
        ('config load ' + $ioc.Replace('\','/')),
        ('project path ' + $roleRoot.Replace('\','/')),
        ('project setCustomFWPath ' + $firmware.Replace('\','/')),
        ('config saveas ' + $ioc.Replace('\','/')),
        'project generate', 'exit'
    )
    [System.IO.File]::WriteAllLines($script, $scriptLines, (New-Object System.Text.UTF8Encoding($false)))
    Push-Location $CubeMXRoot
    try {
        # Windows PowerShell 5 treats native stderr (including Java INFO logs)
        # as an ErrorRecord. Preserve it in the log and use exit/status below.
        $savedErrorAction = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        & $java '--add-opens=java.desktop/java.awt=ALL-UNNAMED' '--add-exports=java.desktop/sun.awt=ALL-UNNAMED' '-Djavax.net.ssl.trustStoreType=WINDOWS-ROOT' '-jar' $cube '-q' $script *> (Join-Path $logRoot ($item.Role + '.log'))
        $ErrorActionPreference = $savedErrorAction
        if ($LASTEXITCODE) { throw ('CubeMX process failed for ' + $item.Role) }
    } finally { $ErrorActionPreference = 'Stop'; Pop-Location }
    $generationLog = Get-Content -LiteralPath (Join-Path $logRoot ($item.Role + '.log')) -Raw
    if ($generationLog -notmatch '(?s)project generate.*\r?\nOK') { throw ('CubeMX generation did not report OK: ' + $item.Role) }
    $projectFile = Join-Path $roleRoot ('MDK-ARM\' + $item.Role + '.uvprojx')
    [xml]$xml = Get-Content -LiteralPath $projectFile -Raw
    $target = $xml.Project.Targets.Target
    foreach ($key in @('pCCUsed','uAC6')) {
        $node = $target.SelectSingleNode($key)
        if (!$node) { $node = $xml.CreateElement($key); $target.AppendChild($node) | Out-Null }
        $node.InnerText = @{pCCUsed='6220000::V6.22::ARMCLANG'; uAC6='1'}[$key]
    }
    $common = $target.TargetOption.TargetCommonOption
    foreach ($key in @('PackID','PackURL')) {
        $node = $common.SelectSingleNode($key)
        if (!$node) { $node = $xml.CreateElement($key); $common.AppendChild($node) | Out-Null }
        $node.InnerText = @{PackID=$item.Pack; PackURL='https://www.keil.com/pack/'}[$key]
    }
    $common.OutputDirectory = '.\' + $item.Role + '\'
    $common.ListingPath = $common.OutputDirectory
    $cads = $target.TargetOption.TargetArmAds.Cads
    $node = $cads.SelectSingleNode('v6Lang')
    if (!$node) { $node = $xml.CreateElement('v6Lang'); $cads.AppendChild($node) | Out-Null }
    $node.InnerText = '1'
    $xml.Save($projectFile)
    Write-Output ('Generated ' + $projectFile)
}
