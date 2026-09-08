param(
    [Parameter(Mandatory = $true)]
    [string]$DllPath,
    [string]$ObsRoot = "$env:ProgramFiles\obs-studio"
)

$ErrorActionPreference = "Stop"
$DllPath = (Resolve-Path $DllPath).Path
$Target = Join-Path $ObsRoot "obs-plugins\64bit\obs-social-comments.dll"
$TargetDir = Split-Path $Target -Parent

if (!(Test-Path $ObsRoot)) {
    throw "No encontré OBS en: $ObsRoot"
}

New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
Copy-Item $DllPath $Target -Force
Write-Host "Instalado: $Target"
Write-Host "Cierra y vuelve a abrir OBS. Luego ve a Docks > Social Comments."
