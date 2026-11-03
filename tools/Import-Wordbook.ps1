param(
 [string]$InputFile = 'imex/1.txt',
 [string]$SettingsPath = 'bin/1.5/settings.json',
 [ValidateRange(0,1000000)][int]$Limit = 0,
 [ValidateRange(0,300)][double]$DelaySeconds = 1.5,
 [ValidateRange(0,5)][int]$Retries = 2,
 [ValidateRange(1,32)][int]$Concurrency = 1,
 [switch]$ForceRefresh,
 [switch]$DryRun
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
function Resolve-ProjectPath([string]$value){if([IO.Path]::IsPathRooted($value)){[IO.Path]::GetFullPath($value)}else{[IO.Path]::GetFullPath((Join-Path $root $value))}}
$inputPath=Resolve-ProjectPath $InputFile
$settings=Resolve-ProjectPath $SettingsPath
if(!(Test-Path -LiteralPath $inputPath -PathType Leaf)){throw '单词本文件不存在。'}
if(!(Test-Path -LiteralPath $settings -PathType Leaf)){throw 'settings.json 不存在。'}
$out=Split-Path $settings
$assembly=Join-Path $out 'MyDict.exe'
if(!(Test-Path -LiteralPath $assembly)){throw '请先编译程序，或指定程序目录中的 settings.json。'}
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -products * -version '[17.0,18.0)' -property installationPath
if(!$vs){throw '未找到 VS2022。'}
& (Join-Path $vs 'MSBuild/Current/Bin/Roslyn/csc.exe') /nologo /platform:x64 ('/out:'+(Join-Path $out 'WordbookImport.exe')) ('/r:'+$assembly) /r:System.Net.Http.dll /r:System.Web.Extensions.dll (Join-Path $PSScriptRoot 'WordbookImport.cs')
if($LASTEXITCODE -ne 0){throw '导入脚本编译失败。'}
& (Join-Path $out 'WordbookImport.exe') $inputPath $settings $Limit ([int]($DelaySeconds*1000)) $Retries $ForceRefresh.IsPresent.ToString() $DryRun.IsPresent.ToString() $Concurrency
if($LASTEXITCODE -eq 2){Write-Warning '部分词条失败，详情保存在 words-shared.sqlite 的 import_errors 表；再次执行可重试。'}
elseif($LASTEXITCODE -ne 0){throw '导入已停止；查看上方错误，成功结果可在下次运行时恢复。'}
