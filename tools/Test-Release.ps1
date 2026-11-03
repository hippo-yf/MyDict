param([string]$OutputDirectory = 'bin\1.5')
$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$installation = & $vswhere -latest -products * -version '[17.0,18.0)' -property installationPath
if (-not $installation) { throw 'Visual Studio 2022 was not found.' }
$root = Split-Path $PSScriptRoot
$out = if ([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $root $OutputDirectory }
& (Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe') (Join-Path $root 'MyDict.sln') /p:Configuration=Release /p:Platform=x64 ('/p:OutputPath=' + $out + '\') /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
$csc = Join-Path $installation 'MSBuild\Current\Bin\Roslyn\csc.exe'
$refs = 'C:\Program Files (x86)\Reference Assemblies\Microsoft\Framework\.NETFramework\v4.8'
foreach ($test in @('SenseTests', 'CacheTests', 'ReleaseUiTests','DeepSeekTests','CacheSyncTests','SqliteTests','FontTests')) {
 $arguments = @('/nologo', '/debug:full','/optimize-', '/platform:x64', ('/out:' + (Join-Path $out ($test + '.exe'))), ('/r:' + (Join-Path $out 'MyDict.exe')), '/r:System.Web.Extensions.dll','/r:System.Net.Http.dll')
 if ($test -in @('ReleaseUiTests','DeepSeekTests','FontTests')) { foreach ($assembly in @('PresentationFramework','PresentationCore','WindowsBase','System.Xaml')) { $arguments += '/r:' + (Join-Path $refs ($assembly + '.dll')) } }
 $arguments += Join-Path $root ('tests\' + $test + '.cs')
 & $csc @arguments
 if ($LASTEXITCODE -ne 0) { throw ('Test compilation failed: ' + $test) }
 & (Join-Path $out ($test + '.exe'))
 if ($LASTEXITCODE -ne 0) { throw ('Test failed: ' + $test) }
}



# The batch importer is tested with a fake HTTP handler; this never calls the paid API.
& $csc /nologo /platform:x64 ('/out:' + (Join-Path $out 'WordbookImport.exe')) ('/r:' + (Join-Path $out 'MyDict.exe')) /r:System.Net.Http.dll /r:System.Web.Extensions.dll (Join-Path $root 'tools/WordbookImport.cs')
if ($LASTEXITCODE -ne 0) { throw 'Importer compilation failed.' }
& $csc /nologo /platform:x64 ('/out:' + (Join-Path $out 'WordbookImportTests.exe')) ('/r:' + (Join-Path $out 'MyDict.exe')) ('/r:' + (Join-Path $out 'WordbookImport.exe')) /r:System.Net.Http.dll /r:System.Web.Extensions.dll (Join-Path $root 'tests/WordbookImportTests.cs')
if ($LASTEXITCODE -ne 0) { throw 'Importer test compilation failed.' }
$importOutput = & (Join-Path $out 'WordbookImportTests.exe')
if ($LASTEXITCODE -ne 0) { $importOutput | Write-Output; throw 'Importer tests failed.' }
$importOutput | Select-Object -Last 1 | Write-Output
