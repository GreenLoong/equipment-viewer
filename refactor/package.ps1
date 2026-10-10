param(
    [Parameter(Mandatory = $true)][string]$OutputRoot,
    [string]$BuildDirectory = 'build-refactor',
    [switch]$Force,
    [switch]$SkipCadRuntime,
    [switch]$SkipSmokeTest
)

# 生成可直接双击运行“设备查看器.exe”的交付目录：
# 重构版可执行文件（含嵌入图标）+ Qt/编译器运行库 + 插件 + CAD 私有运行库 + 虚构样例 + 技术文档。
# 只复制运行所需文件；不复制源码、测试程序、data 数据库与任何凭证。
$ErrorActionPreference = 'Stop'
$refactorRoot = $PSScriptRoot
$projectRoot = Split-Path -Parent $refactorRoot
$buildDir = [System.IO.Path]::GetFullPath($(if ([System.IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $projectRoot $BuildDirectory }))
if (-not $buildDir.StartsWith([System.IO.Path]::GetFullPath($projectRoot).TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw '构建目录必须位于当前工程的独立子目录。'
}
$bin = Join-Path $buildDir 'bin'
$executable = Join-Path $bin 'qt_sqlite_demo.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "请先构建：$executable 不存在。" }

$cmakeText = [System.IO.File]::ReadAllText((Join-Path $refactorRoot 'CMakeLists.txt'))
if ($cmakeText -notmatch 'VERSION\s+(\d+\.\d+\.\d+)') { throw '无法读取重构版版本号。' }
$version = $Matches[1]

$target = [System.IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $target) {
    $existing = Get-ChildItem -LiteralPath $target -Force
    if ($existing.Count -gt 0) {
        # 只允许刷新本脚本自己生成的目录，避免覆盖用户文件。
        if (-not (Test-Path -LiteralPath (Join-Path $target 'manifest.json') -PathType Leaf) -or -not $Force) {
            throw "目标目录非空且不是本脚本生成的交付目录，已停止：$target（如需刷新本脚本产物请加 -Force）"
        }
    }
} else {
    New-Item -ItemType Directory -Path $target | Out-Null
}

# 交付目录不携带资料库：刷新时清掉上一次运行/试用产生的 data 目录（仅限本脚本生成的交付目录）。
$dataDirectory = Join-Path $target 'data'
if (Test-Path -LiteralPath $dataDirectory -PathType Container) {
    Remove-Item -LiteralPath $dataDirectory -Recurse -Force
    Write-Output "REMOVED: $dataDirectory（交付目录不携带资料库）"
}

# 只复制程序真正加载的运行库：Qt6Test 仅测试程序使用，不进交付目录。
$applicationName = '设备查看器.exe'
Copy-Item -LiteralPath $executable -Destination (Join-Path $target $applicationName) -Force
Get-ChildItem -LiteralPath $bin -File -Filter '*.dll' | Where-Object { $_.Name -ne 'Qt6Test.dll' } | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $target $_.Name) -Force
}
# 清掉早期交付版本可能残留的测试专用运行库：目标目录是本脚本产物，刷新后应只含运行所需文件。
$staleTestLibrary = Join-Path $target 'Qt6Test.dll'
if (Test-Path -LiteralPath $staleTestLibrary -PathType Leaf) {
    Remove-Item -LiteralPath $staleTestLibrary -Force
    Write-Output "REMOVED: $staleTestLibrary（仅测试程序使用）"
}
foreach ($plugin in @('platforms', 'sqldrivers', 'styles', 'imageformats', 'iconengines', 'generic', 'networkinformation', 'tls')) {
    $source = Join-Path $bin $plugin
    if (Test-Path -LiteralPath $source -PathType Container) {
        Copy-Item -LiteralPath $source -Destination (Join-Path $target $plugin) -Recurse -Force
    }
}
Copy-Item -LiteralPath (Join-Path $refactorRoot 'resources\app.ico') -Destination (Join-Path $target '设备查看器.ico') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'samples') -Destination (Join-Path $target 'samples') -Recurse -Force

$documentation = Join-Path $target '文档'
New-Item -ItemType Directory -Path $documentation -Force | Out-Null
Copy-Item -Path (Join-Path $refactorRoot 'docs\package\*.md') -Destination $documentation -Force
Copy-Item -LiteralPath (Join-Path $refactorRoot 'docs\dedup-report.md') `
    -Destination (Join-Path $documentation '04-重复实现审查与去重报告.md') -Force
Copy-Item -LiteralPath (Join-Path $refactorRoot 'resources\app-icon-preview.png') `
    -Destination (Join-Path $documentation '图标预览.png') -Force

$launch = @'
@echo off
cd /d "%~dp0"
"%~dp0设备查看器.exe" --database "%~dp0data\viewer.sqlite"
if errorlevel 1 pause
'@
[System.IO.File]::WriteAllText((Join-Path $target '启动设备查看器.cmd'), $launch, [System.Text.Encoding]::ASCII)

$sourceCommit = (& git -c "safe.directory=$projectRoot" -C $projectRoot rev-parse HEAD).Trim()
$sourceChanges = (& git -c "safe.directory=$projectRoot" -C $projectRoot status --porcelain)
$manifest = [ordered]@{
    product = '设备模型查看器（去重重构版）'
    version = $version
    sourceCommit = $sourceCommit
    sourceHasUncommittedChanges = [bool]$sourceChanges
    generatedAt = (Get-Date).ToString('o')
    databaseIncluded = $false
    cadRuntimeIncluded = -not $SkipCadRuntime
    smokeTest = 'pending'
    executable = $applicationName
    executableSha256 = (Get-FileHash -LiteralPath (Join-Path $target $applicationName) -Algorithm SHA256).Hash
    iconSha256 = (Get-FileHash -LiteralPath (Join-Path $target '设备查看器.ico') -Algorithm SHA256).Hash
    fileCount = 0
    totalBytes = 0
}
# 先落盘清单：即使后续 CAD 同步或冒烟失败，目录仍是本脚本可刷新的产物。
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $target 'manifest.json') -Encoding UTF8

if (-not $SkipCadRuntime) {
    & (Join-Path $projectRoot 'scripts\sync-cad-runtime.ps1') -Destination (Join-Path $target 'cad-runtime')
    if ($LASTEXITCODE -ne 0) { throw 'CAD 运行库同步失败。' }
}

$smokeResult = 'skipped'
if (-not $SkipSmokeTest) {
    # 包内冒烟：使用临时数据库与 offscreen 平台，验证交付目录本身能启动并截图后退出。
    $screenshot = Join-Path ([System.IO.Path]::GetTempPath()) ("viewer-package-smoke-" + [guid]::NewGuid().ToString('N') + ".png")
    $stdout = Join-Path ([System.IO.Path]::GetTempPath()) ("viewer-package-smoke-" + [guid]::NewGuid().ToString('N') + ".txt")
    $process = Start-Process -FilePath (Join-Path $target $applicationName) -WorkingDirectory $target `
        -ArgumentList @('-platform', 'offscreen', '--smoke-test', $screenshot) -PassThru -Wait `
        -RedirectStandardOutput $stdout -RedirectStandardError ($stdout + '.err')
    $smokeResult = "exit=$($process.ExitCode) screenshotExists=$(Test-Path -LiteralPath $screenshot)"
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $screenshot)) {
        throw "包内冒烟启动失败：$smokeResult（详见 $stdout）"
    }
    Remove-Item -LiteralPath $screenshot -Force
    Remove-Item -LiteralPath $stdout, ($stdout + '.err') -Force -ErrorAction SilentlyContinue
}

$files = Get-ChildItem -LiteralPath $target -Recurse -File -Force
$manifest.smokeTest = $smokeResult
$manifest.fileCount = $files.Count
$manifest.totalBytes = ($files | Measure-Object -Sum Length).Sum
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $target 'manifest.json') -Encoding UTF8

Write-Output "DELIVERY: $target"
Write-Output ("FILES: {0}, SIZE: {1:N1} MB" -f $manifest.fileCount, ($manifest.totalBytes / 1MB))
Write-Output ("EXE SHA256: {0}" -f $manifest.executableSha256)
Write-Output ("SMOKE: {0}" -f $manifest.smokeTest)
