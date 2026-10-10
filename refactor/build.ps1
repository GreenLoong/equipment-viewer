param(
    [string]$QtRoot = 'D:\Qt\6.11.1\mingw_64',
    [string]$MinGWBin = 'D:\Qt\Tools\mingw1310_64\bin',
    [string]$CMake = 'D:\Qt\Tools\CMake_64\bin\cmake.exe',
    [string]$Ninja = 'D:\Qt\Tools\Ninja\ninja.exe',
    [string]$BuildDirectory = 'build-refactor',
    [switch]$SkipCadRuntime
)

# 重构版独立构建脚本：源码目录为本目录（refactor/），测试与样例沿用工程根目录的现有文件。
# 工具链路径与 scripts/build.ps1 保持一致；只改变 -S 指向，不改动原工程脚本。
$ErrorActionPreference = 'Stop'
$refactorRoot = $PSScriptRoot
$projectRoot = Split-Path -Parent $refactorRoot
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) { throw '构建目录不能为空。' }
$buildDir = [System.IO.Path]::GetFullPath($(if ([System.IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $projectRoot $BuildDirectory }))
$projectBoundary = [System.IO.Path]::GetFullPath($projectRoot).TrimEnd('\') + '\'
if (-not $buildDir.StartsWith($projectBoundary, [System.StringComparison]::OrdinalIgnoreCase)) { throw '构建目录必须位于当前工程的独立子目录。' }
$firstSegment = $buildDir.Substring($projectBoundary.Length).Split('\')[0]
if ($firstSegment -in @('src','tests','samples','docs','scripts','tools','api','.git','data','.recovery','refactor')) { throw '构建目录不能指向源码、资料或版本管理目录。' }

$qtBin = Join-Path $QtRoot 'bin'
$compiler = Join-Path $MinGWBin 'g++.exe'
$deployTool = Join-Path $qtBin 'windeployqt.exe'
$ctest = Join-Path (Split-Path -Parent $CMake) 'ctest.exe'
foreach ($tool in @($CMake, $Ninja, $compiler, $deployTool, $ctest)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "找不到工具：$tool。请通过脚本参数指定安装位置。"
    }
}

$previousPath = $env:PATH
try {
    $env:PATH = "$qtBin;$MinGWBin;$previousPath"
    & $CMake --fresh -S $refactorRoot -B $buildDir -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_CXX_COMPILER=$compiler" `
        "-DCMAKE_PREFIX_PATH=$QtRoot" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败。' }
    & $CMake --build $buildDir --parallel 4
    if ($LASTEXITCODE -ne 0) { throw '编译失败。' }

    $binDir = Join-Path $buildDir 'bin'
    & $deployTool --release --no-translations --no-opengl-sw --compiler-runtime `
        --exclude-plugins qsqlibase,qsqlmimer,qsqloci,qsqlodbc,qsqlpsql `
        (Join-Path $binDir 'qt_sqlite_demo.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt 运行依赖部署失败。' }
    & $deployTool --release --no-translations --no-opengl-sw `
        --include-plugins qoffscreen `
        --exclude-plugins qsqlibase,qsqlmimer,qsqloci,qsqlodbc,qsqlpsql `
        (Join-Path $binDir 'demo_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw '测试运行依赖部署失败。' }

    if (-not $SkipCadRuntime) {
        & (Join-Path $projectRoot 'scripts/sync-cad-runtime.ps1') -Destination (Join-Path $binDir 'cad-runtime')
        if ($LASTEXITCODE -ne 0) { throw 'CAD运行库同步失败。' }
    }

    & $ctest --test-dir $buildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw '自动检查失败。' }
    Write-Host "重构版构建和检查完成：$(Join-Path $binDir 'qt_sqlite_demo.exe')"
} finally {
    $env:PATH = $previousPath
}
