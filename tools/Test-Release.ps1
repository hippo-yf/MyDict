$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$installation = & $vswhere -latest -products * -version '[17.0,18.0)' -property installationPath
if (-not $installation) { throw 'Visual Studio 2022 was not found.' }
$root = Split-Path $PSScriptRoot
& (Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe') (Join-Path $root 'MyDict.sln') /p:Configuration=Release /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$out = Join-Path $root 'bin\Unified'
$csc = Join-Path $installation 'MSBuild\Current\Bin\Roslyn\csc.exe'
$refs = 'C:\Program Files (x86)\Reference Assemblies\Microsoft\Framework\.NETFramework\v4.8'
foreach ($test in @('SenseTests', 'CacheTests', 'ReleaseUiTests')) {
 $arguments = @('/nologo', ('/out:' + (Join-Path $out ($test + '.exe'))), ('/r:' + (Join-Path $out 'MyDict.exe')), '/r:System.Web.Extensions.dll')
 if ($test -eq 'ReleaseUiTests') { foreach ($assembly in @('PresentationFramework','PresentationCore','WindowsBase','System.Xaml')) { $arguments += '/r:' + (Join-Path $refs ($assembly + '.dll')) } }
 $arguments += Join-Path $root ('tests\' + $test + '.cs')
 & $csc @arguments
 if ($LASTEXITCODE -ne 0) { throw ('Test compilation failed: ' + $test) }
 & (Join-Path $out ($test + '.exe'))
 if ($LASTEXITCODE -ne 0) { throw ('Test failed: ' + $test) }
}
