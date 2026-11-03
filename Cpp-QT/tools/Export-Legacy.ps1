[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SourceDatabase,[Parameter(Mandatory=$true)][string]$OutputFile,[string]$LegacyExe)
$ErrorActionPreference='Stop'
$qtRoot=Split-Path $PSScriptRoot
$repositoryRoot=Split-Path $qtRoot
if(-not $LegacyExe){$LegacyExe=Join-Path $repositoryRoot 'bin/1.5/MyDict.exe'}
if(-not(Test-Path $LegacyExe)){throw '未找到原 C# 版 MyDict.exe，请用 -LegacyExe 指定。'}
$compiler='C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/Roslyn/csc.exe'
if(-not(Test-Path $compiler)){throw '需要 Windows VS2022 C# 编译工具，仅迁移原二进制格式时使用。'}
$outputDirectory=Join-Path $qtRoot 'build/migration'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$exporter=Join-Path $outputDirectory 'Export-Legacy.exe'
Copy-Item -LiteralPath $LegacyExe -Destination (Join-Path $outputDirectory 'MyDict.exe') -Force
& $compiler /nologo /target:exe /platform:x64 /langversion:7.3 "/out:$exporter" "/r:$outputDirectory/MyDict.exe" /r:System.Runtime.Serialization.dll /r:System.Xml.dll /r:System.Web.Extensions.dll "$PSScriptRoot/Export-Legacy.cs" "$repositoryRoot/Sqlite.cs"
if($LASTEXITCODE){throw '旧词库迁移工具编译失败。'}
& $exporter ([IO.Path]::GetFullPath($SourceDatabase)) ([IO.Path]::GetFullPath($OutputFile))
if($LASTEXITCODE){throw '导出失败，原词库已保留。'}
