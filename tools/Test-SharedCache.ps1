[CmdletBinding()]
param([string]$CSharpDirectory='bin/Shared',[string]$QtTool='Cpp-QT/build/windows/Release/MyDictTool.exe',[string]$QtPrefix=$env:CONDA_PREFIX)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
function Full([string]$path){if([IO.Path]::IsPathRooted($path)){[IO.Path]::GetFullPath($path)}else{[IO.Path]::GetFullPath((Join-Path $root $path))}}
$out=Full $CSharpDirectory
$tool=Full $QtTool
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs=& $vswhere -latest -products * -version '[17.0,18.0)' -property installationPath
$csc=Join-Path $vs 'MSBuild/Current/Bin/Roslyn/csc.exe'
& $csc /nologo /platform:x64 /langversion:7.3 ("/out:$out/CrossClientCacheTests.exe") ("/r:$out/MyDict.exe") /r:System.Web.Extensions.dll /r:System.Runtime.Serialization.dll /r:System.Xml.dll "$root/tests/CrossClientCacheTests.cs" "$root/Sqlite.cs"
if($LASTEXITCODE){throw 'Cross-client fixture compilation failed.'}
$oldPath=$env:PATH
try{
    if($QtPrefix){$env:PATH="$QtPrefix/Library/bin;$QtPrefix;$oldPath";$plugins="$QtPrefix/Library/lib/qt6/plugins"}else{$plugins=Split-Path $tool}
    & "$out/CrossClientCacheTests.exe" $tool $plugins
    if($LASTEXITCODE){throw 'Cross-client shared cache test failed.'}
}finally{$env:PATH=$oldPath}
