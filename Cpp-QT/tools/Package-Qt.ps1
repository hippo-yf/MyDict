[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$QtPrefix,[Parameter(Mandatory=$true)][string]$BuildDirectory,[string]$Configuration='Release',[string]$OutputDirectory)
$ErrorActionPreference='Stop'
if(-not$OutputDirectory){$OutputDirectory=Join-Path (Split-Path $PSScriptRoot) ('dist/windows-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
$python=Join-Path $QtPrefix 'python.exe'
$cmake='C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$oldPath=$env:PATH
try {
    $env:PATH="$QtPrefix/Library/bin;$QtPrefix;$cmake;$oldPath"
    & $python "$PSScriptRoot/package-qt.py" --build $BuildDirectory --prefix $QtPrefix --config $Configuration --output $OutputDirectory
    if($LASTEXITCODE){throw 'Qt 打包失败。'}
}finally{$env:PATH=$oldPath}
