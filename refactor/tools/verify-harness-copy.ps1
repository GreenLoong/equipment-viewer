# 校验 refactor/tests 与 refactor/tools 内的测试快照与工程根目录同名文件逐字节一致，
# 防止“重构版跑了被改弱的测试”。只读比较，不修改任何文件。
$ErrorActionPreference = 'Stop'
$refactorRoot = Split-Path -Parent $PSScriptRoot
$projectRoot = Split-Path -Parent $refactorRoot
# 1.2.0 起有意分叉的测试：整体移除“团队共享库/工艺批注”后，这几个文件删掉了相关步骤，
# 与本目录不再逐字节一致，属预期差异，不计入不一致数量。
$diverged = @('tests/cad_user_tests.cpp', 'tests/cad_import_process_tests.cpp', 'tests/reviewer_resource_tests.cpp')
$mismatch = 0
$checked = 0
foreach ($relative in @('tests', 'tools')) {
    if ($relative -eq 'tests') { $files = Get-ChildItem (Join-Path $refactorRoot 'tests') -File -Filter *.cpp }
    else { $files = Get-ChildItem (Join-Path $refactorRoot 'tools') -File -Filter environment_probe.cpp }
    foreach ($file in $files) {
        $key = "$relative/$($file.Name)"
        if ($diverged -contains $key) { Write-Output "diverged-by-design: $key"; continue }
        $original = Join-Path (Join-Path $projectRoot $relative) $file.Name
        if (-not (Test-Path -LiteralPath $original -PathType Leaf)) {
            # 重构树新增的测试（原工程没有对应文件）单独列出，不参与逐字节比对。
            Write-Output "refactor-only: $relative/$($file.Name)"
            continue
        }
        $copiedHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        $originalHash = (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
        $checked++
        if ($copiedHash -ne $originalHash) {
            Write-Output "MISMATCH $relative/$($file.Name)"
            $mismatch++
        }
    }
}
Write-Output "harness copy check: checked=$checked mismatches=$mismatch"
if ($mismatch -ne 0) { exit 1 }
