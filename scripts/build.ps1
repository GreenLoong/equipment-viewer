param(
    [string]$QtRoot = 'D:\Qt\6.11.1\mingw_64',
    [string]$MinGWBin = 'D:\Qt\Tools\mingw1310_64\bin',
    [string]$CMake = 'D:\Qt\Tools\CMake_64\bin\cmake.exe',
    [string]$Ninja = 'D:\Qt\Tools\Ninja\ninja.exe'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot 'build'
$qtBin = Join-Path $QtRoot 'bin'
$compiler = Join-Path $MinGWBin 'g++.exe'
$deployTool = Join-Path $qtBin 'windeployqt.exe'
$ctest = Join-Path (Split-Path -Parent $CMake) 'ctest.exe'
foreach ($tool in @($CMake, $Ninja, $compiler, $deployTool, $ctest)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "找不到工具：$tool。请通过脚本参数指定安装位置。"
    }
}

# 仅改变当前进程的 PATH，退出时恢复，不修改系统环境变量。
$previousPath = $env:PATH
try {
    $env:PATH = "$qtBin;$MinGWBin;$previousPath"
    # 当前安装的 CMake 在复用此中文路径的缓存时异常退出，重新配置可正常工作。
    & $CMake --fresh -S $projectRoot -B $buildDir -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_CXX_COMPILER=$compiler" `
        "-DCMAKE_PREFIX_PATH=$QtRoot" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败。' }
    & $CMake --build $buildDir --parallel 4
    if ($LASTEXITCODE -ne 0) { throw '编译失败。' }

    $binDir = Join-Path $buildDir 'bin'
    # 把运行依赖放在 exe 旁边，支持直接启动。
    & $deployTool --release --no-translations --no-opengl-sw --compiler-runtime `
        --exclude-plugins qsqlibase,qsqlmimer,qsqloci,qsqlodbc,qsqlpsql `
        (Join-Path $binDir 'qt_sqlite_demo.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt 运行依赖部署失败。' }
    & $deployTool --release --no-translations --no-opengl-sw `
        --include-plugins qoffscreen `
        --exclude-plugins qsqlibase,qsqlmimer,qsqloci,qsqlodbc,qsqlpsql `
        (Join-Path $binDir 'demo_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw '测试运行依赖部署失败。' }

    & $ctest --test-dir $buildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw '自动检查失败。' }
    Write-Host "构建和检查完成：$(Join-Path $binDir 'qt_sqlite_demo.exe')"
} finally {
    $env:PATH = $previousPath
}
