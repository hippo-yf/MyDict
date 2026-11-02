param(
 [string]$Word = 'go',
 [string]$SettingsPath = 'bin/1.5/settings.json',
 [string]$ReplayResponsePath
)
# Explicit, opt-in real API test: uses the configured API key and may incur charges.
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products * -version '[17.0,18.0)' -property installationPath
if(!$vs){throw 'VS2022 not found.'}
$out=Join-Path $root 'bin/1.5'
& (Join-Path $vs 'MSBuild/Current/Bin/Roslyn/csc.exe') /nologo ('/out:'+(Join-Path $out 'DeepSeekLiveCheck.exe')) ('/r:'+(Join-Path $out 'MyDict.exe')) /r:System.Net.Http.dll /r:System.Web.Extensions.dll (Join-Path $PSScriptRoot 'DeepSeekLiveCheck.cs')
if($LASTEXITCODE -ne 0){throw 'Live test compilation failed.'}
$settings=if([IO.Path]::IsPathRooted($SettingsPath)){$SettingsPath}else{Join-Path $root $SettingsPath}
$liveArgs=@($settings,$Word)
if($ReplayResponsePath){$liveArgs+=$ReplayResponsePath}
& (Join-Path $out 'DeepSeekLiveCheck.exe') @liveArgs
if($LASTEXITCODE -ne 0){throw 'Live DeepSeek test failed.'}
