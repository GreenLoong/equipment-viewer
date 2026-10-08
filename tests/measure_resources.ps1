param(
    [string]$Executable,
    [ValidateSet('offscreen','windows')][string]$Platform = 'offscreen',
    [ValidateRange(1,20)][int]$Batches = 5,
    [ValidateRange(1,100)][int]$WindowCount = 20,
    [ValidateRange(1,20)][int]$Loads = 5,
    [ValidateRange(4,500)][int]$Requests = 40,
    [ValidateRange(50,2000)][int]$SampleMilliseconds = 250,
    [ValidateRange(30,600)][int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$resourceProject = Split-Path -Parent $PSScriptRoot
if (-not $Executable) { $Executable = Join-Path $resourceProject 'build\bin\reviewer_resource_tests.exe' }
$resourceBinary = (Resolve-Path -LiteralPath $Executable).Path
if (-not (Test-Path -LiteralPath $resourceBinary -PathType Leaf)) { throw '资源测试EXE不存在。' }
$resourceBuild = [System.IO.Path]::GetFullPath((Join-Path $resourceProject 'build'))
$resourceOutput = Join-Path $resourceBuild ('resource-observations-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
if (-not $resourceOutput.StartsWith($resourceBuild + '\', [System.StringComparison]::OrdinalIgnoreCase)) { throw '观察目录越界。' }
New-Item -ItemType Directory -Path $resourceOutput | Out-Null
$resourceStdout = Join-Path $resourceOutput 'counters.jsonl'
$resourceStderr = Join-Path $resourceOutput 'stderr.log'
$resourceArguments = @('-platform',$Platform,'--batches',"$Batches",'--windows',"$WindowCount",'--loads',"$Loads",'--requests',"$Requests")

# 只启动/观察此测试进程，不扫描或终止其他应用；日志保留在忽略的build目录。
$resourceProcess = Start-Process -FilePath $resourceBinary -ArgumentList $resourceArguments -WorkingDirectory $resourceProject -WindowStyle Hidden -RedirectStandardOutput $resourceStdout -RedirectStandardError $resourceStderr -PassThru
$resourceStart = $resourceProcess.StartTime
$resourceClock = [System.Diagnostics.Stopwatch]::StartNew()
$resourceSamples = New-Object 'System.Collections.Generic.List[object]'
$resourceTimedOut = $false
Write-Output ('RESOURCE_OBSERVATIONS: ' + $resourceOutput)
try {
    while (-not $resourceProcess.HasExited) {
        if ($resourceClock.Elapsed.TotalSeconds -gt $TimeoutSeconds) {
            $resourceTimedOut = $true
            # Process对象持有本次启动的句柄；不按进程名/PID批量停止。
            $resourceProcess.Kill()
            if (-not $resourceProcess.WaitForExit(5000)) { throw '本次资源测试进程退出未确认。' }
            break
        }
        try {
            $resourceObserved = Get-Process -Id $resourceProcess.Id -ErrorAction Stop
            if ($resourceObserved.StartTime -eq $resourceStart) {
                $resourceSamples.Add([pscustomobject]@{
                    utc = [DateTime]::UtcNow.ToString('o')
                    elapsedMilliseconds = $resourceClock.ElapsedMilliseconds
                    pid = $resourceProcess.Id
                    handles = $resourceObserved.HandleCount
                    privateBytes = $resourceObserved.PrivateMemorySize64
                    workingSetBytes = $resourceObserved.WorkingSet64
                })
            }
        } catch { if (-not $resourceProcess.HasExited) { throw } }
        finally { if ($resourceObserved) { $resourceObserved.Dispose(); $resourceObserved = $null } }
        Start-Sleep -Milliseconds $SampleMilliseconds
        $resourceProcess.Refresh()
    }
    $resourceProcess.WaitForExit()
    $resourceExit = $resourceProcess.ExitCode
} finally {
    try {
        if (-not $resourceProcess.HasExited) {
            $resourceProcess.Kill()
            if (-not $resourceProcess.WaitForExit(5000)) { throw '观察失败后本次测试进程退出未确认。' }
        }
    } finally {
        $resourceSamples | Export-Csv -LiteralPath (Join-Path $resourceOutput 'process-samples.csv') -NoTypeInformation -Encoding UTF8
    }
}

$resourceRows = @()
foreach ($resourceLine in Get-Content -LiteralPath $resourceStdout -Encoding UTF8) {
    if ($resourceLine.StartsWith('{')) { $resourceRows += ($resourceLine | ConvertFrom-Json) }
}
$resourceEnd = @($resourceRows | Where-Object { $_.phase -eq 'complete' } | Select-Object -Last 1)
$resourceManifest = [ordered]@{
    executable = $resourceBinary
    executableSha256 = (Get-FileHash -LiteralPath $resourceBinary -Algorithm SHA256).Hash
    platform = $Platform
    batches = $Batches
    windowsPerBatch = $WindowCount
    loadsPerWindow = $Loads
    requestsPerBatch = $Requests
    pid = $resourceProcess.Id
    exitCode = $resourceExit
    timedOut = $resourceTimedOut
    samples = $resourceSamples.Count
    logicalCountersAccepted = ($resourceEnd.Count -eq 1 -and $resourceEnd[0].accepted -eq 1 -and $resourceExit -eq 0)
    memoryTrendInterpretation = '观察PrivateBytes/句柄暖机后批次趋势；不是ASan或零泄漏保证。'
}
$resourceManifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $resourceOutput 'manifest.json') -Encoding UTF8
Write-Output ('RESOURCE_OBSERVATIONS: ' + $resourceOutput)
$resourceProcess.Dispose()
if ($resourceTimedOut -or $resourceExit -ne 0 -or -not $resourceManifest.logicalCountersAccepted) {
    throw '资源测试未通过，请检查保留的stderr与计数日志。'
}
Write-Output '资源逻辑计数检查通过；PrivateBytes/句柄趋势仍需依据批次实际数据判断。'
