# Snapshots PsuIlluminus.ini on every change and records which PSU programs were running.
param(
    [int]$Minutes = 180,
    [string]$OutDir = "D:\work\PSUEx\tools\ini-snapshots"
)

$ini = Join-Path $env:LOCALAPPDATA "SEGA\PHANTASY STAR UNIVERSE Illuminus\PsuIlluminus.ini"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$log = Join-Path $OutDir "changes.log"

function Get-InputSections( [string]$path ) {
    $text = Get-Content $path -Raw -ErrorAction SilentlyContinue
    if( -not $text ) { return "" }
    $m = [regex]::Matches( $text, '(?ms)^\[(gamepad1|keyboard)\].*?(?=^\[|\z)' )
    ( $m | ForEach-Object { $_.Value.Trim() } ) -join "`n"
}

$lastHash = ( Get-FileHash $ini -ErrorAction SilentlyContinue ).Hash
$lastInput = Get-InputSections $ini
$stamp = Get-Date -Format "HHmmss"
Copy-Item $ini ( Join-Path $OutDir "$stamp-initial.ini" ) -ErrorAction SilentlyContinue
Add-Content $log "$(Get-Date -Format T) watching $ini (hash $lastHash)"

$deadline = ( Get-Date ).AddMinutes( $Minutes )
while( ( Get-Date ) -lt $deadline ) {
    Start-Sleep -Milliseconds 500
    $hash = ( Get-FileHash $ini -ErrorAction SilentlyContinue ).Hash
    if( -not $hash -or $hash -eq $lastHash ) { continue }

    $procs = ( Get-Process PSUC, online, option -ErrorAction SilentlyContinue | ForEach-Object { $_.ProcessName } | Sort-Object -Unique ) -join ","
    $input = Get-InputSections $ini
    $inputChanged = if( $input -ne $lastInput ) { "INPUT SECTIONS CHANGED" } else { "input unchanged" }
    $stamp = Get-Date -Format "HHmmss"
    Copy-Item $ini ( Join-Path $OutDir "$stamp.ini" ) -ErrorAction SilentlyContinue
    Add-Content $log "$(Get-Date -Format T) ini written; running=[$procs]; $inputChanged; snapshot $stamp.ini"

    $lastHash = $hash
    $lastInput = $input
}
Add-Content $log "$(Get-Date -Format T) watcher stopped"
