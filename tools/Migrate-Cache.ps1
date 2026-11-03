param([string]$SettingsPath='bin/1.5/settings.json')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$settings=if([IO.Path]::IsPathRooted($SettingsPath)){$SettingsPath}else{Join-Path $root $SettingsPath}
$out=Split-Path $settings
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products * -version '[17.0,18.0)' -property installationPath
& (Join-Path $vs 'MSBuild/Current/Bin/Roslyn/csc.exe') /nologo /platform:x64 ('/out:'+(Join-Path $out 'DatabaseMaintenance.exe')) ('/r:'+(Join-Path $out 'MyDict.exe')) /r:System.Web.Extensions.dll (Join-Path $PSScriptRoot 'DatabaseMaintenance.cs')
if($LASTEXITCODE -ne 0){throw 'Maintenance tool compilation failed.'}
& (Join-Path $out 'DatabaseMaintenance.exe') $settings
if($LASTEXITCODE -ne 0){throw 'SQLite migration/check failed; original files retained.'}
