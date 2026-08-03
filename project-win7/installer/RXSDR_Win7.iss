; RXSDR_Win7.iss — Instalador para Windows 7, 8, 10 e 11
; Executavel linkado com CRT estatico (sem vcruntime DLL).
; Inclui TODAS as DLLs necessarias: RTL-SDR, SDRplay, Decoders.
; Compile com: ISCC RXSDR_Win7.iss

#define MyAppName      "RXSDR"
#define MyAppVersion   "1.0.25"
#define MyAppPublisher "PU1XTB — Ruben"
#define MyAppURL       "https://github.com/ruben/RXSDR"
#define MyAppExeName   "RXSDR.exe"

; Pasta do executavel e web (build sem Qt)
#define BuildDir       "..\build-win7"

; Pasta dos decoders (DLLs + executaveis + configs)
#define DecDir         "..\build-win7\decoders"

[Setup]
AppId={{A1B2C3D4-5E6F-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion} (Win7+)
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={pf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=output
OutputBaseFilename=RXSDR_Setup_{#MyAppVersion}_Win7
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupIconFile=assets\app.ico
WizardImageFile=assets\wizard.png
WizardSmallImageFile=assets\logo_small.png

; Compatibilidade: Windows 7 SP1 (6.1.7601) e superior, 64-bit
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=6.1.7601

; Instala em Program Files (C:\Arquivos de Programas no Win7 PT-BR)
; Requer admin pois Program Files e protegida pelo sistema
PrivilegesRequired=admin

UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "brazilian"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "english";   MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Criar atalho na &Area de Trabalho"; GroupDescription: "Atalhos:"; Flags: checkedonce
Name: "autostart";   Description: "Iniciar o {#MyAppName} automaticamente com o Windows"; GroupDescription: "Inicializacao:"; Flags: unchecked

[Files]
; ── Executavel principal (CRT estatico — sem vcruntime.dll) ─────────────────
Source: "{#BuildDir}\RXSDR.exe"; DestDir: "{app}"; Flags: ignoreversion

; ── Icone ───────────────────────────────────────────────────────────────────
Source: "assets\app.ico"; DestDir: "{app}"; Flags: ignoreversion

; ── Interface Web (HTML/JS/CSS) ──────────────────────────────────────────────
; Excludes protege a distribuicao: backups de desenvolvimento (.bak) nunca
; podem ir para o instalador do usuario final.
Source: "{#BuildDir}\web\*"; DestDir: "{app}\web"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.bak,*.bak_*,*_bak_*,*.old,*.tmp"

; ────────────────────────────────────────────────────────────────────────────
; DLLs RTL-SDR (obrigatorias — sem elas o app nao abre)
; ────────────────────────────────────────────────────────────────────────────
Source: "{#BuildDir}\librtlsdr.dll";  DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\rtlsdr.dll";     DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\libusb-1.0.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\pthreadVC2.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\msvcr100.dll";   DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; ── SDRplay API (carregada dinamicamente — opcional) ────────────────────────
Source: "{#BuildDir}\sdrplay_api.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; ────────────────────────────────────────────────────────────────────────────
; Pasta DECODERS — executaveis, DLLs e configs de todos os decoders
; (DSD-FME, ACARS, AIS-Catcher, DSDPlus, FMP24, FMPA, FMPP, Survey)
; ────────────────────────────────────────────────────────────────────────────
Source: "{#DecDir}\*"; DestDir: "{app}\decoders"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist


[Icons]
Name: "{group}\{#MyAppName}";                          Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\app.ico"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}";    Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}";                    Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\app.ico"; Tasks: desktopicon

[Registry]
; ── Iniciar com o Windows (somente se marcado pelo usuario) ──────────────────
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "{#MyAppName}"; \
    ValueData: """{app}\{#MyAppExeName}"""; \
    Flags: uninsdeletevalue; Tasks: autostart

; ── Limpa o serial salvo para que o usuario configure o PROPRIO dongle ───────
; Evita que instalacoes anteriores (ou do desenvolvedor) contaminem a config.
; O app detecta automaticamente o dongle conectado no primeiro uso.
Root: HKCU; Subkey: "Software\PU1XTB\RXSDR\device"; \
    ValueType: string; ValueName: "serial"; \
    ValueData: ""; \
    Flags: createvalueifdoesntexist uninsdeletevalue

; Define o tipo padrao como rtlsdr (abre o 1o dongle encontrado automaticamente)
Root: HKCU; Subkey: "Software\PU1XTB\RXSDR\device"; \
    ValueType: string; ValueName: "last"; \
    ValueData: "rtlsdr"; \
    Flags: createvalueifdoesntexist

[UninstallRun]
Filename: "taskkill.exe"; Parameters: "/f /im {#MyAppExeName}"; Flags: runhidden
Filename: "netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#MyAppName} HTTP"""; Flags: runhidden
Filename: "netsh.exe"; Parameters: "advfirewall firewall delete rule name=""{#MyAppName} WebSocket"""; Flags: runhidden

[Run]
; Libera as portas 8080 (HTTP) e 8081 (WebSocket) no Firewall do Windows
; Necessario para que a interface web funcione corretamente
Filename: "netsh.exe"; \
    Parameters: "advfirewall firewall add rule name=""{#MyAppName} HTTP"" dir=in action=allow protocol=TCP localport=8080 program=""{app}\{#MyAppExeName}"" enable=yes"; \
    Flags: runhidden; StatusMsg: "Configurando firewall (porta HTTP)..."
Filename: "netsh.exe"; \
    Parameters: "advfirewall firewall add rule name=""{#MyAppName} WebSocket"" dir=in action=allow protocol=TCP localport=8081 program=""{app}\{#MyAppExeName}"" enable=yes"; \
    Flags: runhidden; StatusMsg: "Configurando firewall (porta WebSocket)..."

Filename: "{app}\{#MyAppExeName}"; Description: "Iniciar o {#MyAppName} agora"; \
    Flags: nowait postinstall skipifsilent

Filename: "https://www.sdrplay.com/api/"; \
    Description: "Baixar API SDRplay (para hardware RSP1/RSP1A/RSP2/RSPduo/RSPdx)"; \
    Flags: shellexec postinstall skipifsilent unchecked

[Code]
procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then
  begin
    WizardForm.FinishedLabel.Caption :=
      'O {#MyAppName} foi instalado com sucesso!' + #13#10 + #13#10 +
      'Esta versao funciona no Windows 7, 8, 10 e 11 (64-bit).' + #13#10 +
      'Nao requer Qt nem Visual C++ Redistributable.' + #13#10 + #13#10 +
      'PARA RTL-SDR:' + #13#10 +
      '  Conecte o dongle RTL-SDR antes de abrir o programa.' + #13#10 +
      '  Instale o driver WinUSB via Zadig: https://zadig.akeo.ie/' + #13#10 +
      '  (selecione o dongle e clique "Install Driver")' + #13#10 + #13#10 +
      'PARA RTL-TCP:' + #13#10 +
      '  Configure o host e porta no painel de Setup.' + #13#10 + #13#10 +
      'PARA SDRplay (RSP1/RSP1A/RSP1B/RSP2/RSPduo/RSPdx):' + #13#10 +
      '  Instale a API oficial em: https://www.sdrplay.com/api/' + #13#10 + #13#10 +
      'DECODERS DIGITAIS (DMR/P25/ACARS/AIS/etc.):' + #13#10 +
      '  Os executáveis, DLLs e dependências já estão inclusos' + #13#10 +
      '  na pasta do programa (' + ExpandConstant('{app}') + '\decoders\).';
  end;
end;
