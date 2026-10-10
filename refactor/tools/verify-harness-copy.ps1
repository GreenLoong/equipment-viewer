# 校验 refactor/tests 与 refactor/tools 内的测试快照与工程根目录同名文件逐字节一致，
# 防止“重构版跑了被改弱的测试”。只读比较，不修改任何文件。
$ErrorActionPreference = 'Stop'
$refactorRoot = Split-Path -Parent $PSScriptRoot
$projectRoot = Split-Path -Parent $refactorRoot
$mismatch = 0
$checked = 0
foreach ($relative in @('tests', 'tools')) {
    if ($relative -eq 'tests') { $files = Get-ChildItem (Join-Path $refactorRoot 'tests') -File -Filter *.cpp }
    else { $files = Get-ChildItem (Join-Path $refactorRoot 'tools') -File -Filter environment_probe.cpp }
    foreach ($file in $files) {
        $original = Join-Path (Join-Path $projectRoot $relative) $file.Name
        if (-not (Test-Path -LiteralPath $original -PathType Leaf)) { throw "根目录缺少对应文件：$original" }
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
