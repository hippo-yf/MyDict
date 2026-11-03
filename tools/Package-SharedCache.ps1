[CmdletBinding()]
param([string]$CSharpDirectory='bin/Shared',[string]$OutputDirectory='Cpp-QT/dist/shared-csharp')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
function Full([string]$path){if([IO.Path]::IsPathRooted($path)){[IO.Path]::GetFullPath($path)}else{[IO.Path]::GetFullPath((Join-Path $root $path))}}
$source=Full $CSharpDirectory
$out=Full $OutputDirectory
$name='MyDict-v1.5.1-shared-windows-x64'
$stage=Join-Path $out $name
if(Test-Path -LiteralPath $stage){throw '请指定一个新的输出目录，避免混入已有用户文件。'}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
foreach($file in @('MyDict.exe','MyDict.exe.config','SharedCacheMaintenance.exe')){Copy-Item -LiteralPath (Join-Path $source $file) -Destination $stage}
foreach($file in @('LICENSE','THIRD-PARTY-NOTICES.md')){Copy-Item -LiteralPath (Join-Path $root $file) -Destination $stage}
Copy-Item -LiteralPath "$root/Cpp-QT/settings.qt.example.json" -Destination "$stage/settings.example.json"
New-Item -ItemType Directory -Path "$stage/fonts","$stage/docs","$stage/configs" -Force | Out-Null
Get-ChildItem -LiteralPath "$root/fonts" -File | Copy-Item -Destination "$stage/fonts"
Copy-Item -LiteralPath "$root/Cpp-QT/docs/SHARED_WORDBOOK.md" -Destination "$stage/docs"
Copy-Item -LiteralPath "$root/Cpp-QT/configs/shared-user.example.json" -Destination "$stage/configs"
@'
拾词 MyDict 1.5.1-shared — C# / Windows x64

运行 MyDict.exe。需要 Windows 10/11 x64 和 .NET Framework 4.8。
这是支持公共缓存协议的共享构建，固定旧版 v1.5.1 不支持该协议。

各端在 settings.json 中设置同一 username、同一 CacheSync.Repository，
并配置本机自己的 Git/SSH。不要复制其他系统的私钥路径。
本地缓存：profiles/<username>/words-shared.sqlite。
远端缓存：users/<username>/shared/{cs|qt}-<device>.sqlite。

从旧 C# 版升级可保留原目录设置和 profiles，仅替换程序；旧 words.sqlite 会只读迁移。
也可执行：SharedCacheMaintenance.exe 原settings.json路径 --data-dir 新程序目录 --sync
旧数据库和旧远端快照保留，切勿为升级先删除它们。

本包不含个人配置、API Key、SSH 密钥或词库。
作者 Yance Feng & Codex；应用 MIT 许可。
'@ | Set-Content "$stage/START-HERE.txt" -Encoding utf8
$zip=Join-Path $out ($name+'.zip')
Compress-Archive -Path "$stage/*" -DestinationPath $zip
$sum=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+[IO.Path]::GetFileName($zip)
$sum | Set-Content ($zip+'.sha256') -Encoding ascii
Write-Output $zip
