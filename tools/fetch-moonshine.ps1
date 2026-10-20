# Fetch the Moonshine speech-to-text library and the bundled English model into
# third_party\moonshine (git-ignored). Needed to build tools\PSUExSpeech and to package a release.
#   lib:   moonshine-voice-windows-x86_64 (v0.1.5, ~26 MB): include\, lib\ (static /MD libs + onnxruntime.dll)
#   model: small-streaming-en (~135 MB) from download.moonshine.ai
param(
    [string]$Version = "v0.1.5",
    [string[]]$Models = @( "small" )
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "third_party\moonshine"
New-Item -ItemType Directory -Force $dest | Out-Null

if( -not ( Test-Path ( Join-Path $dest "lib\moonshine.lib" ) ) ) {
    $tgz = Join-Path $env:TEMP "moonshine-voice-windows-x86_64.tar.gz"
    Write-Host "Downloading the Moonshine library $Version..."
    Invoke-WebRequest "https://github.com/moonshine-ai/moonshine/releases/download/$Version/moonshine-voice-windows-x86_64.tar.gz" -OutFile $tgz
    tar -xzf $tgz -C $dest --strip-components=1
    Remove-Item $tgz
}

# Same file list and CDN directory as the helper's --download (tools\PSUExSpeech\main.cpp).
$files = @( "adapter.ort", "cross_kv.ort", "decoder_kv.ort", "encoder.ort", "frontend.model.ort",
    "frontend.weights.ort", "streaming_config.json", "tokenizer.bin" )
foreach( $size in $Models ) {
    $dir = Join-Path $dest "models\$size-streaming-en"
    New-Item -ItemType Directory -Force $dir | Out-Null
    foreach( $f in $files ) {
        $out = Join-Path $dir $f
        if( Test-Path $out ) { continue }
        Write-Host "Downloading $size/$f..."
        Invoke-WebRequest "https://download.moonshine.ai/model/$size-streaming-en/quantized_26_08_21/$f" -OutFile $out
    }
}
Write-Host "Moonshine ready in $dest"
