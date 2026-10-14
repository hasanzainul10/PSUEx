param(
    [string]$GameDir = "D:\Games\PSUClementine_22072025",
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# Prefer freshly built DLL from MSBuild OutDir
$candidates = @(
    ( Join-Path $root "Debug\PSUExD.dll" ),
    ( Join-Path $root "Win32\Debug\PSUExD.dll" ),
    ( Join-Path $root "x86\Debug\PSUExD.dll" )
)
$dll = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if( -not $dll ) { throw "PSUExD.dll not found. Build Debug|x86 first." }

Copy-Item $dll ( Join-Path $GameDir "PSUExD.dll" ) -Force

$loaderDll = Join-Path $root "tools\dinput8_proxy\dinput8.dll"
if( Test-Path $loaderDll ) {
    Copy-Item $loaderDll ( Join-Path $GameDir "dinput8.dll" ) -Force
}

$oxr = Join-Path $root "third_party\openxr\bin\openxr_loader.dll"
if( Test-Path $oxr ) {
    Copy-Item $oxr $GameDir -Force
}

$d3dx = "D:\PSUExTools\DXSDK\..\dxsdk_d3dx\out\build\native\release\bin\x86\D3DX9_43.dll"
if( -not ( Test-Path $d3dx ) ) {
    $d3dx = "D:\PSUExTools\dxsdk_d3dx\out\build\native\release\bin\x86\D3DX9_43.dll"
}
if( Test-Path $d3dx ) {
    Copy-Item $d3dx $GameDir -Force
}

Write-Host "Deployed to $GameDir"
Write-Host "Launch via online.exe (not PSUC.exe directly), then check psuex.log / dinput8_proxy.log"
