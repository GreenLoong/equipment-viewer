# 校验 refactor/tests 与 refactor/tools 内的测试快照与重构前的实现一致，防止“重构版跑了被改弱的测试”。
# 只读比较，不修改任何文件。工作目录里存在旧实现的同名文件时直接逐字节比对；否则改用 -LegacyRef
# 指定的本机历史引用做 blob 比对（git hash-object 与索引使用同样的行尾规范化，结论与逐字节比较
# 等价）。-LegacyRef 只存在于开发机，不随仓库分发；引用缺失时明确报告 no-legacy-reference，
# 而不是静默跳过。
param(
    [string]$LegacyRef = 'v1.1.1'
)
$ErrorActionPreference = 'Stop'
$refactorRoot = Split-Path -Parent $PSScriptRoot
$projectRoot = Split-Path -Parent $refactorRoot
$legacyRef = $LegacyRef
$gitArgs = @('-c', "safe.directory=$projectRoot", '-C', $projectRoot)
# 1.2.0 起有意分叉的测试：整体移除“团队共享库/工艺批注”后，这几个文件删掉了相关步骤，
# 输出与注释里的内部术语也做了清理，与 1.1.1 不再逐字节一致，属预期差异，不计入不一致数量。
$diverged = @('tests/cad_user_tests.cpp', 'tests/cad_import_process_tests.cpp', 'tests/reviewer_resource_tests.cpp',
    'tests/desktop_assembly_user_tests.cpp', 'tests/r07_window_tests.cpp', 'tests/r11_desktop_tester.cpp')
$mismatch = 0
$checked = 0
foreach ($relative in @('tests', 'tools')) {
    if ($relative -eq 'tests') { $files = Get-ChildItem (Join-Path $refactorRoot 'tests') -File -Filter *.cpp }
    else { $files = Get-ChildItem (Join-Path $refactorRoot 'tools') -File -Filter environment_probe.cpp }
    foreach ($file in $files) {
        $key = "$relative/$($file.Name)"
        if ($diverged -contains $key) { Write-Output "diverged-by-design: $key"; continue }
        $original = Join-Path (Join-Path $projectRoot $relative) $file.Name
        if (Test-Path -LiteralPath $original -PathType Leaf) {
            $copiedHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
            $originalHash = (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
        } else {
            $copiedHash = (& git @gitArgs hash-object --path="$key" $file.FullName).Trim()
            # rev-parse 在目标不存在时会把参数原样回显，必须先用 cat-file -e 判断存在性。
            $null = & git @gitArgs cat-file -e "${legacyRef}:$key" 2>$null
            if ($LASTEXITCODE -ne 0) { Write-Output "no-legacy-reference: $key (标签 $legacyRef 缺失或该文件不存在)"; continue }
            $originalHash = (& git @gitArgs rev-parse "${legacyRef}:$key").Trim()
        }
        $checked++
        if ($copiedHash -ne $originalHash) {
            Write-Output "MISMATCH $key"
            $mismatch++
        }
    }
}
Write-Output "harness copy check: checked=$checked mismatches=$mismatch (legacy ref: $legacyRef)"
if ($mismatch -ne 0) { exit 1 }
