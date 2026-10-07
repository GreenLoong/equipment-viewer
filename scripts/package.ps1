param([Parameter(Mandatory = $true)][string]$OutputRoot, [string]$Version)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmakeText = [System.IO.File]::ReadAllText((Join-Path $projectRoot 'CMakeLists.txt'))
if ($cmakeText -notmatch 'project\([^\r\n]*VERSION\s+(\d+\.\d+\.\d+)') { throw '无法读取项目版本。' }
$actualVersion = $Matches[1]
if ($Version -and $Version -ne $actualVersion) { throw '禁止用打包参数伪造与源码不同的版本。' }
$Version = $actualVersion
$bin = Join-Path $projectRoot 'build\bin'
$executable = Join-Path $bin 'qt_sqlite_demo.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw '请先构建并验证应用，再生成交付包。'
}
# 每次生成独立目录，避免覆盖用户已经使用的交付包和运行资料。
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$bundleName = "equipment-viewer-$Version-$stamp"
$bundle = Join-Path $OutputRoot $bundleName
if (Test-Path -LiteralPath $bundle) { throw "交付目录已存在：$bundle" }
New-Item -ItemType Directory -Path $bundle | Out-Null
Copy-Item -LiteralPath $executable -Destination (Join-Path $bundle 'qt_sqlite_demo.exe')
Get-ChildItem -LiteralPath $bin -File -Filter '*.dll' | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $bundle $_.Name)
}
# 只复制部署所需插件和虚构样例，不复制data、测试EXE或本机配置。
foreach ($plugin in @('platforms','sqldrivers','styles','imageformats','iconengines','generic','networkinformation','tls')) {
    $source = Join-Path $bin $plugin
    if (Test-Path -LiteralPath $source -PathType Container) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $bundle $plugin) -Recurse
    }
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'samples') -Destination (Join-Path $bundle 'samples') -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\user-guide.md') -Destination (Join-Path $bundle '使用说明.md')
$launch = @'
@echo off
cd /d "%~dp0"
"%~dp0qt_sqlite_demo.exe" --equipment-view --database "%~dp0data\viewer.sqlite"
if errorlevel 1 pause
'@
[System.IO.File]::WriteAllText((Join-Path $bundle '启动设备查看器.cmd'), $launch, [System.Text.Encoding]::ASCII)
$sourceCommit = (& git -c "safe.directory=$projectRoot" -C $projectRoot rev-parse HEAD)
if ($LASTEXITCODE -ne 0) { throw '无法读取源码提交，停止打包。' }
$sourceChanges = (& git -c "safe.directory=$projectRoot" -C $projectRoot status --porcelain)
if ($LASTEXITCODE -ne 0) { throw '无法核对源码状态，停止打包。' }
$manifest = [ordered]@{
    version = $Version
    sourceCommit = $sourceCommit
    sourceHasUncommittedChanges = [bool]$sourceChanges
    generatedAt = (Get-Date).ToString('o')
    databaseIncluded = $false
    executableSha256 = (Get-FileHash -LiteralPath (Join-Path $bundle 'qt_sqlite_demo.exe') -Algorithm SHA256).Hash
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $bundle 'manifest.json') -Encoding UTF8
$zip = Join-Path $OutputRoot ($bundleName + '.zip')
Compress-Archive -LiteralPath $bundle -DestinationPath $zip
Write-Output "BUNDLE: $bundle"
Write-Output "ZIP: $zip"
