#ifndef MyAppVersion
  #define MyAppVersion "dev"
#endif

#define MyAppName "Easy IRL Stream"
#define MyAppPublisher "Easy IRL Stream"
#define MyAppURL "https://github.com/nils-kt/Easy-IRL-Stream"

[Setup]
AppId={{B5E8A3D1-C7F2-4A96-9E5D-1F3B8A6C0D4E}
AppName={#MyAppName} (OBS Plugin)
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={code:GetOBSDir}
DirExistsWarning=no
DisableProgramGroupPage=yes
OutputDir=..\release
OutputBaseFilename=easy-irl-stream-{#MyAppVersion}-windows-installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName={#MyAppName} (OBS Plugin)
SourceDir=..

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[Files]
Source: "build\easy-irl-stream.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "data\locale\en-US.ini"; DestDir: "{app}\data\obs-plugins\easy-irl-stream\locale"; Flags: ignoreversion
Source: "data\locale\de-DE.ini"; DestDir: "{app}\data\obs-plugins\easy-irl-stream\locale"; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{app}\data\obs-plugins\easy-irl-stream"

[Messages]
english.WelcomeLabel2=This will install the {#MyAppName} plugin for OBS Studio.%n%nPlease close OBS Studio before continuing.
german.WelcomeLabel2=Dies installiert das {#MyAppName} Plugin f%C3%BCr OBS Studio.%n%nBitte schlie%C3%9Fe OBS Studio vor der Installation.

[Code]
function GetOBSDir(Param: String): String;
var
  Path: String;
begin
  if RegQueryStringValue(HKLM, 'SOFTWARE\OBS Studio', '', Path) then
    Result := Path
  else
    Result := ExpandConstant('{autopf}\obs-studio');
end;

function IsOBSRunning(): Boolean;
var
  ResultCode: Integer;
begin
  Exec('tasklist', '/FI "IMAGENAME eq obs64.exe" /NH', '', SW_HIDE,
       ewWaitUntilTerminated, ResultCode);
  Result := (ResultCode = 0);
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    if FindWindowByClassName('OBSMainWindow') <> 0 then
    begin
      MsgBox('OBS Studio is currently running. Please close it before continuing.', mbError, MB_OK);
      Abort;
    end;
  end;
end;
