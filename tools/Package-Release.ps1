$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$version = '1.5.1'
$folder = Join-Path $root ('release\MyDict-v' + $version + '-win-x64')
New-Item -ItemType Directory -Path $folder -Force | Out-Null
foreach ($name in @('MyDict.exe','MyDict.exe.config')) {
 Copy-Item -LiteralPath (Join-Path $root ('bin\1.5\' + $name)) -Destination (Join-Path $folder $name) -Force
}
foreach ($name in @('LICENSE','THIRD-PARTY-NOTICES.md','settings.example.json')) { Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $folder $name) -Force }
$fonts=Join-Path $folder 'fonts'
New-Item -ItemType Directory -Path $fonts -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $root 'fonts') -File | Copy-Item -Destination $fonts -Force
@'
MyDict v1.5.1
Authors: Yance Feng & Codex

Extract to a writable folder and run MyDict.exe. Requires 64-bit Windows 10/11 and .NET Framework 4.8 (Windows SQLite runtime).
The app creates settings.json next to the executable.
Cache, favorites, history and per-source entries share profiles/<username>/words.sqlite.
Changes commit immediately in the background. Vocabulary is paged on demand. Automatic Gitee sync is batched after 30 minutes; Git and SSH are required. Legacy JSON is migrated in the background and retained as a backup.
This archive contains no developer settings, API keys or query history.

See settings.example.json for configuration examples, and THIRD-PARTY-NOTICES.md for online content notices.
'@ | Set-Content (Join-Path $folder 'START-HERE.txt') -Encoding UTF8
$zip = Join-Path $root ('release\MyDict-v' + $version + '-win-x64.zip')
Compress-Archive -Path (Join-Path $folder '*') -DestinationPath $zip -Force
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
$checksumPath=Join-Path $root 'release\SHA256SUMS.txt'
$archiveName=[IO.Path]::GetFileName($zip)
$existing=if(Test-Path -LiteralPath $checksumPath){@(Get-Content -LiteralPath $checksumPath | Where-Object { $_ -and !$_.EndsWith('  '+$archiveName) })}else{@()}
$checksum=$hash+'  '+$archiveName
@($existing)+@($checksum) | Set-Content -LiteralPath $checksumPath -Encoding ASCII
$checksum | Set-Content -LiteralPath ($zip+'.sha256') -Encoding ASCII
Write-Output $zip

