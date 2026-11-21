[CmdletBinding()]
param(
    [string]$Executable,
    [string]$QtPrefix=$env:CONDA_PREFIX,
    [string]$DataSnapshot,
    [string]$Username='memory-test',
    [string]$OutputDirectory,
    [ValidateRange(10,60)][int]$DurationSeconds=20,
    [switch]$CompareDirectWrite
)
$ErrorActionPreference='Stop'
if($env:OS -ne 'Windows_NT'){throw '此测量脚本仅用于 Windows。'}
$projectRoot=Split-Path $PSScriptRoot
if(-not $Executable){$Executable=Join-Path $projectRoot 'build/windows/Release/MyDictQt.exe'}
$Executable=[IO.Path]::GetFullPath($Executable)
if(-not(Test-Path -LiteralPath $Executable)){throw '未找到 Qt 程序，请先编译或指定 -Executable。'}
if($Username -notmatch '^[a-z0-9_-]{1,64}$'){throw 'Username 必须为合法的词库身份。'}
if(-not $OutputDirectory){$OutputDirectory=Join-Path $projectRoot ('build/memory-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,6))}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw '请指定一个新的输出目录，避免改写已有数据。'}
if($DataSnapshot){
    $DataSnapshot=[IO.Path]::GetFullPath($DataSnapshot)
    if(-not(Test-Path -LiteralPath $DataSnapshot)){throw '未找到 SQLite 快照。'}
    $python=if($QtPrefix){Join-Path $QtPrefix 'python.exe'}else{(Get-Command python -ErrorAction Stop).Source}
    if(-not(Test-Path -LiteralPath $python)){throw '备份 SQLite 需要 Python 3，可通过 -QtPrefix 指定 Conda 环境。'}
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$oldPath=$env:PATH;$oldPlugins=$env:QT_PLUGIN_PATH;$oldPlatform=$env:QT_QPA_PLATFORM
$runs=@();$samples=@()
try {
    if($QtPrefix){$env:PATH="$QtPrefix/Library/bin;$QtPrefix;$oldPath";$env:QT_PLUGIN_PATH="$QtPrefix/Library/lib/qt6/plugins"}
    Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    $types=@('empty');if($DataSnapshot){$types+='cached'}
    foreach($type in $types){
        $engines=@('default');if($CompareDirectWrite){$engines+='directwrite'}
        foreach($engine in $engines){
            $case="$type-$engine";$data=Join-Path $OutputDirectory $case
            New-Item -ItemType Directory -Path $data | Out-Null
            $config=Get-Content (Join-Path $projectRoot 'settings.qt.example.json') -Raw | ConvertFrom-Json
            $config.username=$Username;$config.CacheSync.Enabled=$false;$config.DeepSeek.Enabled=$true;$config.DeepSeek.ApiKey=''
            $config | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $data 'settings.json') -Encoding utf8
            $fonts=Join-Path $data 'fonts';New-Item -ItemType Directory -Path $fonts | Out-Null
            Get-ChildItem -LiteralPath (Join-Path $projectRoot 'fonts') -File |
                Where-Object {$_.Extension -in @('.ttf','.otf','.ttc','.otc')} | Copy-Item -Destination $fonts
            if($type -eq 'cached'){
                $profile=Join-Path $data ('profiles/'+$Username);New-Item -ItemType Directory -Path $profile | Out-Null
                $backup=@'
import pathlib, sqlite3, sys
source=sqlite3.connect(pathlib.Path(sys.argv[1]).as_uri()+'?mode=ro',uri=True)
target=sqlite3.connect(sys.argv[2])
source.backup(target)
print('Isolated SQLite backup: %d words' % target.execute('select count(*) from words').fetchone()[0])
target.close(); source.close()
'@
                & $python -c $backup $DataSnapshot (Join-Path $profile 'words-shared.sqlite')
                if($LASTEXITCODE){throw '创建只读 SQLite 副本失败。'}
            }
            $arguments=@('--data-dir',('"'+$data+'"'))
            if($engine -eq 'directwrite'){$arguments+=@('-platform','windows:fontengine=directwrite')}
            $process=Start-Process -FilePath $Executable -ArgumentList $arguments -WindowStyle Hidden -PassThru
            $runs+=@{Case=$case;Process=$process;Directory=$data}
        }
    }
    $timer=[Diagnostics.Stopwatch]::StartNew()
    while($timer.Elapsed.TotalSeconds -lt $DurationSeconds){
        foreach($run in $runs){
            $run.Process.Refresh()
            if($run.Process.HasExited){throw ('测试窗口提前退出：'+$run.Case)}
            $samples+=[pscustomobject]@{Case=$run.Case;Seconds=[Math]::Round($timer.Elapsed.TotalSeconds,1);PrivateMB=[Math]::Round($run.Process.PrivateMemorySize64/1MB,1);WorkingSetMB=[Math]::Round($run.Process.WorkingSet64/1MB,1)}
        }
        Start-Sleep -Milliseconds 500
    }
    $result=foreach($run in $runs){
        $values=@($samples | Where-Object {$_.Case -eq $run.Case});$last=$values[-1]
        [pscustomobject]@{Case=$run.Case;PrivateMB=$last.PrivateMB;WorkingSetMB=$last.WorkingSetMB;PeakPrivateMB=($values|Measure-Object PrivateMB -Maximum).Maximum}
    }
    $samples | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'samples.json') -Encoding utf8
    $result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'memory.json') -Encoding utf8
    $result | Format-Table -AutoSize
    Write-Output "Memory report: $OutputDirectory"
} finally {
    foreach($run in $runs){
        $run.Process.Refresh()
        if(-not $run.Process.HasExited){[void]$run.Process.CloseMainWindow();if(-not $run.Process.WaitForExit(5000)){$run.Process.Kill();$run.Process.WaitForExit()}}
        $run.Process.Dispose()
    }
    $env:PATH=$oldPath;$env:QT_PLUGIN_PATH=$oldPlugins;$env:QT_QPA_PLATFORM=$oldPlatform
}
