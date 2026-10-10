param([string]$Database, [switch]$EquipmentView, [string]$BuildDirectory = 'build-1.1')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) { throw '构建目录不能为空。' }
$buildDir = [System.IO.Path]::GetFullPath($(if ([System.IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $projectRoot $BuildDirectory }))
$projectBoundary = [System.IO.Path]::GetFullPath($projectRoot).TrimEnd('\') + '\'
if (-not $buildDir.StartsWith($projectBoundary, [System.StringComparison]::OrdinalIgnoreCase)) { throw '构建目录必须位于当前工程的独立子目录，不能使用工程根目录或外部目录。' }
$firstSegment = $buildDir.Substring($projectBoundary.Length).Split('\')[0]
if ($firstSegment -in @('src','tests','samples','docs','scripts','tools','api','.git','data','.recovery')) { throw '构建目录不能指向源码、资料或版本管理目录。' }
$executable = Join-Path $buildDir 'bin\qt_sqlite_demo.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw '所选目录尚未构建，请先运行 .\scripts\build.ps1 -BuildDirectory build-1.1（或你指定的构建目录）。'
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
