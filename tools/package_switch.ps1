# Puts the Switch build in switch_sd/switch/black, the folder the SD card's switch/black mirrors. The game files
# stay where the earlier port has them (sdmc:/switch/black/game): only black.nro changes on the card.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$nro = Join-Path $root 'build-switch\black.nro'
if (-not (Test-Path -LiteralPath $nro)) { throw "no build: $nro" }
$dest = Join-Path $root 'switch_sd\switch\black'
New-Item -ItemType Directory -Path $dest -Force | Out-Null
Copy-Item -LiteralPath $nro -Destination (Join-Path $dest 'black.nro')
$hash = (Get-FileHash -LiteralPath $nro -Algorithm SHA256).Hash
Write-Host "black.nro -> $dest"
Write-Host "SHA-256 $hash"
Write-Host 'On the card: sdmc:/switch/black/black.nro, the game in sdmc:/switch/black/game.'
