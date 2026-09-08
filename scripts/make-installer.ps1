param(
    [Parameter(Mandatory=$true)][string]$DllPath,
    [string]$Version = "0.4.0",
    [string]$OutputDir = "",
    [string]$IsccPath = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not (Test-Path $DllPath)) { throw "DLL not found: $DllPath" }

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $ProjectRoot "release"
}
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

$Stage = Join-Path $ProjectRoot "release/windows-x64"
$PluginDir = Join-Path $Stage "obs-plugins/64bit"
New-Item -ItemType Directory -Path $PluginDir -Force | Out-Null
Copy-Item -Path $DllPath -Destination (Join-Path $PluginDir "obs-social-comments.dll") -Force

if ([string]::IsNullOrWhiteSpace($IsccPath)) {
    [array]$Candidates = @(.
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    ) | Where-Object { $_ -and (Test-Path $_) }
    if ($Candidates.Count -eq 0) { throw "Inno Setup 6 (ISCC.exe) not found." }
    $IsccPath = $Candidates[0]
}

$Iss = Join-Path $ProjectRoot "installer/social-comments.iss"
& $IsccPath "/DSourceRoot=$Stage" "/DOutputDir=$OutputDir" "/DAppVersion=$Version" $Iss
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

Get-ChildItem -Path $OutputDir -Filter "Social-Comments-for-OBS-v$Version-Setup-x64.exe" | Select-Object -ExpandProperty FullName
