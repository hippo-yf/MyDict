$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$version = '1.5.0-dev'
$folder = Join-Path $root ('release\MyDict-v' + $version + '-win')
New-Item -ItemType Directory -Path $folder -Force | Out-Null
foreach ($name in @('MyDict.exe','MyDict.exe.config')) {
 Copy-Item -LiteralPath (Join-Path $root ('bin\1.5\' + $name)) -Destination (Join-Path $folder $name) -Force
}
foreach ($name in @('LICENSE','THIRD-PARTY-NOTICES.md','settings.example.json')) { Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $folder $name) -Force }
@'
MyDict v1.5.0-dev
Authors: Yance Feng & Codex

Extract to a writable folder and run MyDict.exe. Requires Windows and .NET Framework 4.8.
The app creates settings.json and lookup-cache.json next to the executable.
Favorites are stored at %LOCALAPPDATA%\MyDict\favorites.json.
This archive contains no developer settings, API keys or query history.

See settings.example.json for configuration examples, and THIRD-PARTY-NOTICES.md for online content notices.
'@ | Set-Content (Join-Path $folder 'START-HERE.txt') -Encoding UTF8
$zip = Join-Path $root ('release\MyDict-v' + $version + '-win.zip')
Compress-Archive -Path (Join-Path $folder '*') -DestinationPath $zip -Force
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
($hash + '  ' + [IO.Path]::GetFileName($zip)) | Set-Content (Join-Path $root 'release\SHA256SUMS.txt') -Encoding ASCII
Write-Output $zip

