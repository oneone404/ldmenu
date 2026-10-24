param([string]$OutputPath='C:\Users\OOP\Documents\XuanZhi9\Pictures\LDMenu.exe',[switch]$SkipTests)
$ErrorActionPreference='Stop'
$uiCompiler='D:\App\.tools\llvm-mingw-20260922-ucrt-x86_64\bin\clang++.exe'
$uiBuildDirectory=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $uiBuildDirectory -Force | Out-Null
$uiObjects=@()
foreach ($uiSource in @('main','backend','settings')) {
    $uiObject=Join-Path $uiBuildDirectory ($uiSource+'.o')
    & $uiCompiler --target=x86_64-w64-mingw32 -std=c++17 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -Oz -Wall -Wextra -c (Join-Path $PSScriptRoot ($uiSource+'.cpp')) -o $uiObject
    if($LASTEXITCODE){throw ('LDMenu compile failed: '+$uiSource)}
    $uiObjects+=$uiObject
}
$uiResource=Join-Path $uiBuildDirectory 'resource.o'
& (Join-Path (Split-Path $uiCompiler) 'x86_64-w64-mingw32-windres.exe') -I $PSScriptRoot -i (Join-Path $PSScriptRoot 'app.rc') -o $uiResource
if($LASTEXITCODE){throw 'LDMenu icon resource failed'}
$uiObjects+=$uiResource
Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($OutputPath)) -ErrorAction SilentlyContinue | Stop-Process -Force
& $uiCompiler --target=x86_64-w64-mingw32 -static -municode -mwindows -s '-Wl,--no-insert-timestamp' @uiObjects -ld2d1 -ldwrite -ldwmapi -lbcrypt -lpsapi -lshell32 -lole32 -luuid -o $OutputPath
if($LASTEXITCODE){throw 'LDMenu build failed'}
if(!$SkipTests){
    $testExe=Join-Path $uiBuildDirectory 'unit_tests.exe'
    $settingsObj=Join-Path $uiBuildDirectory 'settings.o'
    & $uiCompiler --target=x86_64-w64-mingw32 -std=c++17 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -Oz -Wall -Wextra -static -s (Join-Path $PSScriptRoot 'tests\unit_tests.cpp') $settingsObj -lbcrypt -lpsapi -lole32 -lshell32 -luuid -o $testExe
    if($LASTEXITCODE){throw 'Unit test build failed'}
    & $testExe | Out-Host
    if($LASTEXITCODE){throw 'Backend tests failed'}
}
$uiAssetSource='D:\App\diagnostics\nph_standalone_research\loaded-libmod-arm64.private.so'
# Full pinned digest; never download a new library implicitly.
$uiAssetHash='1eb769fb80291fae60215276575acecc5363c59e9433965014c6767ffefecd2a'
if((Get-FileHash -LiteralPath $uiAssetSource -Algorithm SHA256).Hash -ne $uiAssetHash){throw 'Private library hash mismatch'}
$uiAssetDirectory=Join-Path (Split-Path -Parent $OutputPath) 'LDMenu'
New-Item -ItemType Directory -Path $uiAssetDirectory -Force | Out-Null
Copy-Item -LiteralPath $uiAssetSource -Destination (Join-Path $uiAssetDirectory 'libmod-arm64.so') -Force
Get-Item -LiteralPath $OutputPath | Select-Object FullName,Length
Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256
