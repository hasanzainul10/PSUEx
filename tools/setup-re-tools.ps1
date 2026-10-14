<#
.SYNOPSIS
    Installs the reverse-engineering toolchain for the PSUEx VR work and registers GhidraMCP with Cursor.

.DESCRIPTION
    Installs (skipping anything already present):
      - Eclipse Temurin JDK 21 (winget)      required by Ghidra
      - uv (winget)                          runs the GhidraMCP server
      - Ghidra 12.1.4                        pinned: GhidraMCP 2.0 is built for exactly this version
      - GhidraMCP 2.0 extension              installed into Ghidra's user Extensions folder
      - UPX (latest)                         unpacks Clementine's PSUC.exe
      - x64dbg snapshot (latest)             x32dbg for debugging the 32-bit client
      - RenderDoc (winget)                   DX9 frame capture

    Then adds "ghidra" to <repo>/.cursor/mcp.json.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\setup-re-tools.ps1
    powershell -ExecutionPolicy Bypass -File tools\setup-re-tools.ps1 -InstallDir D:\tools\psuex -DryRun
#>
param(
    [string]$InstallDir = "D:\PSUExTools",
    [switch]$SkipRenderDoc,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$GhidraVersion = "12.1.4"
$GhidraTag = "Ghidra_${GhidraVersion}_build"
$GhidraMcpTag = "2.0"
$GhidraMcpSource = "git+https://github.com/LaurieWired/GhidraMCP@$GhidraMcpTag"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BinDir = Join-Path $InstallDir "bin"
$DownloadDir = Join-Path $InstallDir "_downloads"

function Write-Step( [string]$Message ) { Write-Host "`n==> $Message" -ForegroundColor Cyan }
function Write-Skip( [string]$Message ) { Write-Host "    already installed: $Message" -ForegroundColor DarkGray }

function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable( "Path", "Machine" )
    $user = [Environment]::GetEnvironmentVariable( "Path", "User" )
    $env:Path = "$machine;$user"
}

function Add-UserPath( [string]$Dir ) {
    $user = [Environment]::GetEnvironmentVariable( "Path", "User" )
    if( ( $user -split ";" ) -notcontains $Dir ) {
        if( $DryRun ) { Write-Host "    [dry run] add to user PATH: $Dir"; return }
        [Environment]::SetEnvironmentVariable( "Path", ( ( $user.TrimEnd( ";" ) ) + ";$Dir" ), "User" )
        Update-SessionPath
    }
}

function Install-WingetPackage( [string]$Id ) {
    $listed = winget list --id $Id -e --accept-source-agreements 2>$null | Out-String
    if( $listed -match [regex]::Escape( $Id ) ) { Write-Skip $Id; return }
    if( $DryRun ) { Write-Host "    [dry run] winget install $Id"; return }
    winget install --id $Id -e --silent --accept-package-agreements --accept-source-agreements
    if( $LASTEXITCODE -ne 0 ) { throw "winget install $Id failed with exit code $LASTEXITCODE" }
    Update-SessionPath
}

function Get-GitHubAsset( [string]$Repo, [string]$Tag, [string]$Pattern ) {
    $url = if( $Tag ) { "https://api.github.com/repos/$Repo/releases/tags/$Tag" } else { "https://api.github.com/repos/$Repo/releases/latest" }
    $release = Invoke-RestMethod -Uri $url -Headers @{ "User-Agent" = "PSUEx-setup" }
    $asset = $release.assets | Where-Object { $_.name -like $Pattern } | Select-Object -First 1
    if( -not $asset ) { throw "No asset matching '$Pattern' in $Repo release $( $release.tag_name )" }
    return $asset
}

function Save-Asset( $Asset ) {
    $path = Join-Path $DownloadDir $Asset.name
    if( Test-Path $path ) { return $path }
    if( $DryRun ) { Write-Host "    [dry run] download $( $Asset.browser_download_url )"; return $path }
    New-Item -ItemType Directory -Force $DownloadDir | Out-Null
    Write-Host "    downloading $( $Asset.name ) ($( [math]::Round( $Asset.size / 1MB ) ) MB)"
    Invoke-WebRequest -Uri $Asset.browser_download_url -OutFile "$path.part" -UseBasicParsing
    Move-Item "$path.part" $path
    return $path
}

function Expand-To( [string]$Zip, [string]$Destination ) {
    if( $DryRun ) { Write-Host "    [dry run] extract $( Split-Path -Leaf $Zip ) -> $Destination"; return }
    New-Item -ItemType Directory -Force $Destination | Out-Null
    Expand-Archive -Path $Zip -DestinationPath $Destination -Force
}

Write-Host "PSUEx reverse-engineering setup" -ForegroundColor Green
Write-Host "Install dir: $InstallDir$( if( $DryRun ) { '  (dry run, nothing will change)' } )"
if( -not $DryRun ) { New-Item -ItemType Directory -Force $InstallDir, $BinDir | Out-Null }

# --- Java 21 ---------------------------------------------------------------
Write-Step "Java 21 JDK"
$javaOk = $false
if( Get-Command java -ErrorAction SilentlyContinue ) {
    $javaVersion = ( & java -version 2>&1 | Out-String )
    if( $javaVersion -match 'version "(\d+)' -and [int]$Matches[ 1 ] -ge 21 ) { $javaOk = $true; Write-Skip "java $( $Matches[ 1 ] )" }
}
if( -not $javaOk ) { Install-WingetPackage "EclipseAdoptium.Temurin.21.JDK" }

# --- uv --------------------------------------------------------------------
Write-Step "uv (runs the GhidraMCP server)"
if( Get-Command uvx -ErrorAction SilentlyContinue ) { Write-Skip "uvx" } else { Install-WingetPackage "astral-sh.uv" }

# --- Ghidra ----------------------------------------------------------------
Write-Step "Ghidra $GhidraVersion"
$ghidraDir = Join-Path $InstallDir "ghidra_${GhidraVersion}_PUBLIC"
if( Test-Path $ghidraDir ) {
    Write-Skip $ghidraDir
} else {
    $zip = Save-Asset ( Get-GitHubAsset "NationalSecurityAgency/ghidra" $GhidraTag "ghidra_${GhidraVersion}_PUBLIC_*.zip" )
    Expand-To $zip $InstallDir
}
if( -not $DryRun ) {
        [Environment]::SetEnvironmentVariable( "GHIDRA_INSTALL_DIR", $ghidraDir, "User" )
        [Environment]::SetEnvironmentVariable( "UV_CACHE_DIR", ( Join-Path $InstallDir "uv-cache" ), "User" )
    }

# --- GhidraMCP extension ---------------------------------------------------
Write-Step "GhidraMCP $GhidraMcpTag extension"
$extensionsDir = Join-Path $env:APPDATA "ghidra\ghidra_${GhidraVersion}_PUBLIC\Extensions"
if( Test-Path ( Join-Path $extensionsDir "GhidraMCP" ) ) {
    Write-Skip ( Join-Path $extensionsDir "GhidraMCP" )
} else {
    $releaseZip = Save-Asset ( Get-GitHubAsset "LaurieWired/GhidraMCP" $GhidraMcpTag "GhidraMCP-release-*.zip" )
    $releaseDir = Join-Path $DownloadDir "GhidraMCP-release"
    Expand-To $releaseZip $releaseDir
    if( $DryRun ) {
        Write-Host "    [dry run] extract the GhidraMCP extension -> $extensionsDir"
    } else {
        $extensionZip = Get-ChildItem $releaseDir -Recurse -Filter "ghidra_${GhidraVersion}_PUBLIC_*_GhidraMCP.zip" | Select-Object -First 1
        if( -not $extensionZip ) { throw "GhidraMCP release has no extension built for Ghidra $GhidraVersion" }
        Expand-To $extensionZip.FullName $extensionsDir
    }
}

# --- UPX -------------------------------------------------------------------
Write-Step "UPX"
if( Test-Path ( Join-Path $BinDir "upx.exe" ) ) {
    Write-Skip ( Join-Path $BinDir "upx.exe" )
} else {
    $zip = Save-Asset ( Get-GitHubAsset "upx/upx" $null "upx-*-win64.zip" )
    $upxDir = Join-Path $DownloadDir "upx"
    Expand-To $zip $upxDir
    if( -not $DryRun ) { Copy-Item ( Get-ChildItem $upxDir -Recurse -Filter "upx.exe" | Select-Object -First 1 ).FullName $BinDir }
}
Add-UserPath $BinDir

# --- x64dbg (x32dbg) -------------------------------------------------------
Write-Step "x64dbg / x32dbg"
$x64dbgDir = Join-Path $InstallDir "x64dbg"
if( Test-Path $x64dbgDir ) {
    Write-Skip $x64dbgDir
} else {
    $zip = Save-Asset ( Get-GitHubAsset "x64dbg/x64dbg" $null "snapshot_*.zip" )
    Expand-To $zip $x64dbgDir
}

# --- RenderDoc -------------------------------------------------------------
if( -not $SkipRenderDoc ) {
    Write-Step "RenderDoc"
    Install-WingetPackage "BaldurKarlsson.RenderDoc"
}

# --- Warm the GhidraMCP server and register it with Cursor -----------------
Write-Step "GhidraMCP server for Cursor"
$uvx = Get-Command uvx -ErrorAction SilentlyContinue
if( -not $uvx -and -not $DryRun ) { throw "uvx not found on PATH. Restart the terminal and run this script again." }
if( $DryRun ) {
    Write-Host "    [dry run] uvx --from $GhidraMcpSource ghidra-mcp --help"
} else {
    & $uvx.Source --from $GhidraMcpSource ghidra-mcp --help | Out-Null
    if( $LASTEXITCODE -ne 0 ) { throw "Could not fetch the GhidraMCP server through uvx" }
}

$mcpPath = Join-Path $RepoRoot ".cursor\mcp.json"
$mcp = if( Test-Path $mcpPath ) { Get-Content $mcpPath -Raw | ConvertFrom-Json } else { [pscustomobject]@{} }
if( -not $mcp.PSObject.Properties[ "mcpServers" ] ) { $mcp | Add-Member -NotePropertyName mcpServers -NotePropertyValue ( [pscustomobject]@{} ) }
$ghidraServer = [pscustomobject]@{
    command = "uvx"
    args    = @( "--from", $GhidraMcpSource, "ghidra-mcp", "--ghidra-server", "http://127.0.0.1:8080/" )
}
$mcp.mcpServers | Add-Member -NotePropertyName ghidra -NotePropertyValue $ghidraServer -Force
if( $DryRun ) {
    Write-Host "    [dry run] write $mcpPath"
} else {
    New-Item -ItemType Directory -Force ( Split-Path $mcpPath ) | Out-Null
    $mcp | ConvertTo-Json -Depth 10 | Set-Content -Path $mcpPath -Encoding UTF8
}

# --- Summary ---------------------------------------------------------------
Write-Host "`nDone." -ForegroundColor Green
Write-Host @"

Installed:
  Ghidra      $ghidraDir\ghidraRun.bat
  x32dbg      $x64dbgDir\release\x32\x32dbg.exe
  UPX         $BinDir\upx.exe   (on PATH in new terminals)
  RenderDoc   Start menu
  MCP config  $mcpPath

Manual steps left:
  1. Unpack the game exe in the game folder:  upx -d -o PSUC_Uncompressed.exe PSUC.exe
  2. Run ghidraRun.bat, create a project, import PSUC_Uncompressed.exe, and run auto-analysis.
  3. When Ghidra says new plugins were found, enable GhidraMCPPlugin
     (or File > Configure > Developer > GhidraMCPPlugin).
  4. In Cursor: Settings > MCP, turn on the "ghidra" server.
"@
