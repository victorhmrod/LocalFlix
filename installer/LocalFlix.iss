; LocalFlix installer (Inno Setup 6). Build it with scripts\make-installer.cmd, which passes
; AppVersion and SourceDir. Installs per user by default (no administrator rights needed).

#ifndef AppVersion
  #define AppVersion "1.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\installer-staging\LocalFlix"
#endif

[Setup]
AppId={{F9D2B266-3DB9-43A3-A452-94338D20C505}
AppName=LocalFlix
AppVersion={#AppVersion}
AppVerName=LocalFlix {#AppVersion}
AppPublisher=LocalFlix contributors
AppPublisherURL=https://github.com/victorhmrod/LocalFlix
AppSupportURL=https://github.com/victorhmrod/LocalFlix/issues
AppUpdatesURL=https://github.com/victorhmrod/LocalFlix/releases
DefaultDirName={autopf}\LocalFlix
DefaultGroupName=LocalFlix
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=..\dist\installer
OutputBaseFilename=LocalFlix-{#AppVersion}-setup
SetupIconFile=..\resources\localflix.ico
UninstallDisplayIcon={app}\localflix.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}
VersionInfoDescription=LocalFlix Setup
VersionInfoProductName=LocalFlix

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[CustomMessages]
english.RemoveLibraryData=Also delete your LocalFlix library (shows, episode order, watch progress and covers)?%n%nYour video files are never deleted.
brazilianportuguese.RemoveLibraryData=Apagar também a sua biblioteca do LocalFlix (shows, ordem dos episódios, progresso e capas)?%n%nSeus arquivos de vídeo nunca são apagados.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\LocalFlix"; Filename: "{app}\localflix.exe"
Name: "{autodesktop}\LocalFlix"; Filename: "{app}\localflix.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\localflix.exe"; Description: "{cm:LaunchProgram,LocalFlix}"; Flags: nowait postinstall skipifsilent

[Code]
// Uninstalling removes the program only. The library (%APPDATA%\LocalFlix) is kept unless the user
// says otherwise; video files are never touched. Silent uninstalls always keep the library.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    DataDir := ExpandConstant('{userappdata}\LocalFlix');
    if DirExists(DataDir) and (not UninstallSilent) then
      if MsgBox(CustomMessage('RemoveLibraryData'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(DataDir, True, True, True);
  end;
end;
