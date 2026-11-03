[CmdletBinding()]
param([string]$QtPrefix = $env:CONDA_PREFIX, [string]$BuildDirectory = 'build/windows', [ValidateSet('Release','Debug')][string]$Configuration = 'Release', [switch]$SkipTests, [switch]$Package)
$ErrorActionPreference='Stop'
$projectRoot=$PSScriptRoot
if (-not $QtPrefix) {
    throw '请先 conda activate mydict-qt，或通过 -QtPrefix 指定 Conda 环境目录。'
}
$QtPrefix=[IO.Path]::GetFullPath($QtPrefix)
$qtCmake=Join-Path $QtPrefix 'Library'
if (-not (Test-Path (Join-Path $qtCmake 'lib/cmake/Qt6/Qt6Config.cmake'))) {throw '指定环境未包含 Qt6，请按 environment.yml 安装。'}
$cmakeCommand=Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath=if($cmakeCommand){$cmakeCommand.Source}else{'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'}
if(-not(Test-Path $cmakePath)){throw '未找到 CMake。请安装 environment.yml 中的 cmake。'}
$oldPath=$env:PATH
$oldPlugins=$env:QT_PLUGIN_PATH
$oldPlatform=$env:QT_QPA_PLATFORM
try {
    $env:PATH="$QtPrefix/Library/bin;$QtPrefix;$oldPath"
    $env:QT_PLUGIN_PATH="$QtPrefix/Library/lib/qt6/plugins"
    $buildPath=if([IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory}else{Join-Path $projectRoot $BuildDirectory}
    & $cmakePath -S $projectRoot -B $buildPath -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$qtCmake" '-DBUILD_TESTING=ON'
    if($LASTEXITCODE){throw 'CMake 配置失败。'}
    & $cmakePath --build $buildPath --config $Configuration --parallel 4
    if($LASTEXITCODE){throw 'C++ 编译失败。'}
    if(-not $SkipTests){
        $ctest=Join-Path (Split-Path $cmakePath) 'ctest.exe'
        $env:QT_QPA_PLATFORM='offscreen'
        & $ctest --test-dir $buildPath -C $Configuration --output-on-failure
        if($LASTEXITCODE){throw '自动测试失败。'}
        Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    }
    if($Package){& "$projectRoot/tools/Package-Qt.ps1" -QtPrefix $QtPrefix -BuildDirectory $buildPath -Configuration $Configuration}
    Write-Output "Windows x64 编译完成：$buildPath/$Configuration/MyDictQt.exe"
}finally{$env:PATH=$oldPath;$env:QT_PLUGIN_PATH=$oldPlugins;$env:QT_QPA_PLATFORM=$oldPlatform}
