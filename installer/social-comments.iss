#ifndef SourceRoot
  #define SourceRoot "..\\release\\windows-x64"
#endif
#ifndef OutputDir
  #define OutputDir "..\\release"
#endif
#ifndef AppVersion
  #define AppVersion "0.4.0"
#endif

#define AppName "Social Comments for OBS"
#define PluginDll "obs-social-comments.dll"

[Setup]
AppId={{A177B1B9-81E4-4D15-B679-30586A93077F}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Social Comments for OBS
DefaultDirName={code:GetOBSDir}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=Social-Comments-for-OBS-v{#AppVersion}-Setup-x64
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no
Uninstallable=yes
WizardStyle=modern
SetupLogging=yes

[Files]
Source: "{#SourceRoot}\\obs-plugins\\64bit\\{#PluginDll}"; DestDir: "{app}\\obs-plugins\\64bit"; Flags: ignoreversion restartreplace uninsrestartdelete

[Code]
function GetOBSDir(Param: string): string;
var
  InstallLocation: string;
begin
  Result := ExpandConstant('{autopf}\\obs-studio');
  if RegQueryStringValue(HKLM64,
      'SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OBS Studio',
      'InstallLocation', InstallLocation) then
  begin
    if InstallLocation <> '' then
      Result := RemoveBackslashUnlessRoot(InstallLocation);
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  ObsExe: string;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    ObsExe := AddBackslash(WizardDirValue) + 'bin\\64bit\\obs64.exe';
    if not FileExists(ObsExe) then
    begin
      MsgBox('No encontré OBS Studio en esa carpeta. Selecciona la carpeta raíz de OBS (la que contiene bin y obs-plugins).',
        mbError, MB_OK);
      Result := False;
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    Log('Social Comments for OBS instalado. Reinicia OBS si estaba abierto.');
end;
