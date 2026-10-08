param(
    [Parameter(Mandatory=$true)][string]$Destination,
    [string]$BootstrapPython = $env:EQUIPMENT_CAD_BOOTSTRAP_PYTHON,
    [switch]$Offline
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$source=[IO.Path]::GetFullPath((Join-Path $projectRoot '.runtime/cad-distribution/cad-runtime'))
$target=[IO.Path]::GetFullPath($Destination)
if ($target -eq $source -or $target.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase) -or
    $source.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase) -or
    [IO.Path]::GetFileName($target.TrimEnd('\')) -ne 'cad-runtime') {
    throw 'CAD runtime目标必须为独立cad-runtime目录，不能嵌套源目录。'
}
foreach ($required in @('python.exe','python314.zip','python314._pth','site-packages/OCP/__init__.py')) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $required) -PathType Leaf)) { throw 'CAD运行库未准备完成。' }
}
$signature=Get-AuthenticodeSignature -LiteralPath (Join-Path $source 'python.exe')
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Python Software Foundation') {
    throw 'CAD Python签名无效，停止复制。'
}
# 源目录及祖先不得为junction；Python随后检查所有源/目标文件，不跟随未知重解析点。
$currentDirectory=Get-Item -LiteralPath $source
while ($null -ne $currentDirectory) {
    if (($currentDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'CAD源及祖先不能为重解析点。' }
    $currentDirectory=$currentDirectory.Parent
}
if ([string]::IsNullOrWhiteSpace($BootstrapPython)) {
    # 在执行私有Python前核验官方embed全部根文件，不能仅凭exe签名信任其DLL/stdlib。
    $archive=@(
        (Join-Path $projectRoot '.runtime/downloads/cad-bootstrap/python-3.14.8-embed-amd64.zip'),
        (Join-Path $projectRoot '.runtime/downloads/python314/python-3.14.8-embed-amd64.zip')
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    if (-not $archive -or (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne
        'A93ABE456AB01BD96D7A085B3CDB6566B3063F4241360D114142FBDB07F0A310') {
        throw '缺少已核官方embed归档；先用明确受信BootstrapPython准备，不从PATH发现Python。'
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $packed=[IO.Compression.ZipFile]::OpenRead($archive)
    try {
        foreach ($entry in $packed.Entries) {
            if ($entry.FullName.EndsWith('/')) { continue }
            $file=Join-Path $source $entry.FullName
            if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or
                ((Get-Item -LiteralPath $file).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw ('CAD Python根文件缺失或为重解析点：'+$entry.FullName)
            }
            $stream=$entry.Open()
            $algorithm=[Security.Cryptography.SHA256]::Create()
            try { $expected=[BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','') }
            finally { $algorithm.Dispose(); $stream.Dispose() }
            if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $expected) {
                throw ('CAD Python根文件不同：'+$entry.FullName)
            }
        }
    } finally { $packed.Dispose() }
    $BootstrapPython=Join-Path $source 'python.exe'
}
if (-not [IO.Path]::IsPathRooted($BootstrapPython) -or -not (Test-Path -LiteralPath $BootstrapPython -PathType Leaf)) {
    throw 'BootstrapPython必须为明确受信绝对程序路径。'
}
$setup=Join-Path $projectRoot 'tools/cad/setup_runtime.py'
$setupArguments=@('-I',$setup,'sync','--runtime',$source,'--destination',$target)
if ($Offline) { $setupArguments+='--offline' }
# 先完整核验payload，再在独立stage补许可/源码并probe；不同或未知目标保持原样。
& $BootstrapPython @setupArguments
if ($LASTEXITCODE -ne 0) { throw 'CAD运行库同步失败；现有目标和失败stage保留，未做Force覆盖。' }
Write-Output ('CAD_RUNTIME_SYNCED: '+$target)