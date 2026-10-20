# Build the end-user VR release zip: Release|x86 PSUExVR.dll + PSUExVRConfig.exe, the dinput8
# loader, the OpenXR loader, a starter psuex_vr.json, the player guide and licenses.
param(
    [string]$Version = "0.1.0"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# MSBuild: vswhere first, then the local Build Tools install.
$msbuild = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if( Test-Path $vswhere ) {
    $msbuild = & $vswhere -latest -products * -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
}
if( -not $msbuild ) { $msbuild = "D:\VSBuildTools\MSBuild\Current\Bin\MSBuild.exe" }
if( -not ( Test-Path $msbuild ) ) { throw "MSBuild not found" }

Write-Host "Building Release|x86..."
& $msbuild ( Join-Path $root "PSUEx.sln" ) /p:Configuration=Release /p:Platform=x86 /m /v:minimal
if( $LASTEXITCODE -ne 0 ) { throw "MSBuild failed: $LASTEXITCODE" }

Write-Host "Building dinput8 loader..."
& ( Join-Path $root "tools\dinput8_proxy\build.ps1" ) -GameDir ""

$name = "PSUEx-VR-v$Version"
$dist = Join-Path $root "dist\$name"
if( Test-Path $dist ) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force $dist | Out-Null

$files = @(
    @( "Release\PSUExVR.dll", "PSUExVR.dll" ),
    @( "Release\PSUExVRConfig.exe", "PSUExVRConfig.exe" ),
    @( "tools\dinput8_proxy\dinput8.dll", "dinput8.dll" ),
    @( "third_party\openxr\bin\openxr_loader.dll", "openxr_loader.dll" ),
    @( "docs\VR_GUIDE.md", "VR_GUIDE.md" ),
    @( "LICENSE", "LICENSE" ),
    @( "THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md" )
)
foreach( $f in $files ) {
    $src = Join-Path $root $f[0]
    if( -not ( Test-Path $src ) ) { throw "Missing $src" }
    Copy-Item $src ( Join-Path $dist $f[1] )
}

# Speech to text: the 64-bit helper, ONNX Runtime and the bundled small English model (all MIT).
$speechDest = Join-Path $dist "PSUExSpeech"
$modelSrc = Join-Path $root "third_party\moonshine\models\small-streaming-en"
if( -not ( Test-Path ( Join-Path $modelSrc "decoder_kv.ort" ) ) ) { throw "Missing $modelSrc (see third_party\moonshine\README.md)" }
New-Item -ItemType Directory -Force ( Join-Path $speechDest "models\small-streaming-en" ) | Out-Null
Copy-Item ( Join-Path $root "Release\PSUExSpeech\PSUExSpeech.exe" ), ( Join-Path $root "Release\PSUExSpeech\onnxruntime.dll" ) $speechDest
Copy-Item ( Join-Path $modelSrc "*" ) ( Join-Path $speechDest "models\small-streaming-en" )

# Starter settings from the shared serializer, so the file always matches the code.
$config = Join-Path $dist "PSUExVRConfig.exe"
$json = Join-Path $dist "psuex_vr.json"
$proc = Start-Process -FilePath $config -ArgumentList @( "--write-defaults", "`"$json`"" ) -Wait -PassThru
if( $proc.ExitCode -ne 0 -or -not ( Test-Path $json ) ) { throw "Writing psuex_vr.json failed" }

@"
PSUEx VR v$Version (preview)
============================

1. Extract everything in this zip into your PSU Clementine game folder
   (the folder with PSUC.exe and online.exe). Don't touch di.dll.
2. Start SteamVR (PS VR2: start the PlayStation VR2 App on Steam first).
3. Start the game with online.exe.

VR settings: run PSUExVRConfig.exe (game closed), or in game press Ctrl+F10
(or hold Back and press Start).
Recenter: Ctrl+F9 or Back+X.  First/third person: Ctrl+F7 or Back+Y.

Full guide: VR_GUIDE.md (or https://github.com/hasanzainul10/PSUEx-VR/blob/psu_vr/docs/VR_GUIDE.md)
Source code (GPLv3): https://github.com/hasanzainul10/PSUEx-VR
"@ | Set-Content -Path ( Join-Path $dist "README.txt" ) -Encoding UTF8

$zip = Join-Path $root "dist\$name.zip"
if( Test-Path $zip ) { Remove-Item $zip -Force }
Compress-Archive -Path ( Join-Path $dist "*" ) -DestinationPath $zip

Get-ChildItem $dist | Format-Table Name, Length -AutoSize
$size = [math]::Round( ( Get-Item $zip ).Length / 1MB, 2 )
Write-Host "Packaged $zip ($size MB)"
