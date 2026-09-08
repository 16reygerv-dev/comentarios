param(
    [string]$ObsRoot = "$env:ProgramFiles\obs-studio"
)

$ErrorActionPreference = "Stop"
$Target = Join-Path $ObsRoot "obs-plugins\64bit\obs-social-comments.dll"
if (Test-Path $Target) {
    Remove-Item $Target -Force
    Write-Host "Eliminado: $Target"
} else {
    Write-Host "El plugin no estaba instalado en: $Target"
}
