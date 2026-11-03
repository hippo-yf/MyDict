[CmdletBinding()]
param([string]$OutputDirectory='bin/Shared',[switch]$Test)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$out=if([IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory}else{Join-Path $root $OutputDirectory}
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs=& $vswhere -latest -products * -version '[17.0,18.0)' -property installationPath
if(-not$vs){throw '需要 VS2022 和 .NET Framework 4.8 开发组件。'}
if($Test){& "$PSScriptRoot/Test-Release.ps1" -OutputDirectory $out}else{
    & "$vs/MSBuild/Current/Bin/MSBuild.exe" "$root/MyDict.sln" /p:Configuration=Release /p:Platform=x64 "/p:OutputPath=$out/" /v:minimal /nologo
    if($LASTEXITCODE){throw 'C# 共享缓存版编译失败。'}
}
& "$vs/MSBuild/Current/Bin/Roslyn/csc.exe" /nologo /platform:x64 "/out:$out/SharedCacheMaintenance.exe" "/r:$out/MyDict.exe" /r:System.Web.Extensions.dll "$PSScriptRoot/SharedCacheMaintenance.cs"
if($LASTEXITCODE){throw '共享缓存升级工具编译失败。'}
if($Test){
    & "$vs/MSBuild/Current/Bin/Roslyn/csc.exe" /nologo /platform:x64 "/out:$out/SharedMaintenanceTests.exe" "/r:$out/MyDict.exe" /r:System.Web.Extensions.dll "$root/tests/SharedMaintenanceTests.cs"
    if($LASTEXITCODE){throw '升级工具回归测试编译失败。'}
    & "$out/SharedMaintenanceTests.exe" "$out/SharedCacheMaintenance.exe"
    if($LASTEXITCODE){throw '升级工具回归测试失败。'}
}
Write-Output "Shared C# client: $out/MyDict.exe"
