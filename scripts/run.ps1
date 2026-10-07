param([string]$Database, [switch]$EquipmentView)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot 'build\bin\qt_sqlite_demo.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw '尚未构建，请先运行 .\scripts\build.ps1。'
}
if (-not $Database) {
    $Database = Join-Path $projectRoot 'data\records.sqlite'
}
if ($EquipmentView) {
    & $executable --equipment-view --database $Database
} else {
    & $executable --database $Database
}
if ($LASTEXITCODE -ne 0) { throw "程序退出码：$LASTEXITCODE" }
