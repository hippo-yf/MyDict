$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$installation = & $vswhere -latest -products * -version '[17.0,18.0)' -property installationPath
if (-not $installation) { throw '未找到 Visual Studio 2022。' }
& (Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe') (Join-Path $PSScriptRoot 'MyDict.sln') /p:Configuration=Release /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw '编译失败。' }
