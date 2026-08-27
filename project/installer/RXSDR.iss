; -- RXSDR.iss — Script Inno Setup 6 para gerar instalador Windows --
; Compile com: ISCC RXSDR.iss
; O instalador gerado ficará em: project\installer\output\RXSDR_Setup_1.0.0.exe

#define MyAppName      "RXSDR"
#define MyAppVersion   "1.0.58"
#define MyAppPublisher "PU1XTB — Ruben"
#define MyAppURL       "https://github.com/ruben/RXSDR"
#define MyAppExeName   "RXSDR.exe"
#define BuildDir       "..\build"

[Setup]
AppId={{D8E7B3C2-1F45-4A89-A6F0-91234567ABCD}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; Pasta de saída do instalador (relativa ao .iss)
OutputDir=output
OutputBaseFilename=RXSDR_Setup_{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Imagens do assistente de instalação
SetupIconFile=assets\app.ico
WizardImageFile=assets\wizard.png
WizardSmallImageFile=assets\logo_small.png
; Plataforma 64-bit
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\{#MyAppExeName}
; Não mostrar a licença (opcional — descomente se quiser exibir)
; LicenseFile=..\..\LICENSE.txt

[Languages]
Name: "brazilian"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "english";   MessagesFile: "compiler:Default.isl"

[Tasks]
; Atalho na Área de Trabalho — MARCADO por padrão
Name: "desktopicon"; Description: "Criar atalho na &Área de Trabalho"; GroupDescription: "Atalhos adicionais:"; Flags: checkedonce
; Iniciar com o Windows — desmarcado por padrão
Name: "autostart";   Description: "Iniciar o {#MyAppName} automaticamente com o Windows"; GroupDescription: "Inicialização:"; Flags: unchecked

[Files]
; ── Executável principal ──────────────────────────────────────────────────────
Source: "{#BuildDir}\RXSDR.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LICENSE-DSD.txt"; DestDir: "{app}"; Flags: ignoreversion
; Memorias: onlyifdoesntexist para NAO apagar as do usuario ao atualizar.
Source: "..\..\bookmarks.json"; DestDir: "{app}"; Flags: ignoreversion onlyifdoesntexist

; ── Ponte ExtIO (32 bits) - EM ESPERA desde 25/08/2026 ───────────────────────
; Fora do instalador enquanto o suporte a ExtIO nao esta fechado. O codigo
; continua no projeto e o COMPILAR_EXTIO.bat continua gerando a ponte; ela so
; nao viaja para os amigos, para ninguem receber uma pasta que nao serve para
; nada. PARA VOLTAR: apague este comentario e as duas linhas abaixo valem.
; Source: "{#BuildDir}\rxsdr_extio_bridge.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
; Source: "..\extio\LEIAME.txt"; DestDir: "{app}\extio"; Flags: ignoreversion

; ── DLLs Qt6 ─────────────────────────────────────────────────────────────────
Source: "{#BuildDir}\Qt6Core.dll";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Gui.dll";         DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6HttpServer.dll";  DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Network.dll";     DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Svg.dll";         DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6WebSockets.dll";  DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Widgets.dll";     DestDir: "{app}"; Flags: ignoreversion

; ── DLLs DirectX / OpenGL ────────────────────────────────────────────────────
Source: "{#BuildDir}\d3dcompiler_47.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\dxcompiler.dll";     DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\dxil.dll";           DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\opengl32sw.dll";     DestDir: "{app}"; Flags: ignoreversion

; ── DLLs RTL-SDR ─────────────────────────────────────────────────────────────
Source: "{#BuildDir}\librtlsdr.dll";      DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\libusb-1.0.dll";     DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\rtlsdr.dll";         DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\pthreadVC2.dll";     DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\msvcr100.dll";       DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; ── DLLs SDRplay ─────────────────────────────────────────────────────────────
Source: "{#BuildDir}\sdrplay_api.dll";    DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; ── Plugins Qt6 ──────────────────────────────────────────────────────────────
Source: "{#BuildDir}\platforms\*";          DestDir: "{app}\platforms";          Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\imageformats\*";       DestDir: "{app}\imageformats";       Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\iconengines\*";        DestDir: "{app}\iconengines";        Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\styles\*";             DestDir: "{app}\styles";             Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\tls\*";                DestDir: "{app}\tls";                Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\generic\*";            DestDir: "{app}\generic";            Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\translations\*";       DestDir: "{app}\translations";       Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist

; ── Interface Web (HTML/JS/CSS) ───────────────────────────────────────────────
; Excludes protege a distribuicao: backups de desenvolvimento (.bak) nunca
; podem ir para o instalador do usuario final.
Source: "{#BuildDir}\web\*"; DestDir: "{app}\web"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.bak,*.bak_*,*_bak_*,*.old,*.tmp"

; ── Pasta DECODERS — executáveis, DLLs e configs de todos os decoders ────────
; (DSD-FME, ACARS, AIS-Catcher, DSDPlus, FMP24, FMPA, FMPP, Survey, Direwolf)
; O hfdl_compat.h e a pasta hfdl_win_shim so servem para COMPILAR o dumphfdl
; no MSYS2. Nao tem uso nenhum na maquina de quem instala, e mandar codigo
; fonte junto do programa so confunde quem for olhar a pasta.
Source: "{#BuildDir}\decoders\*"; DestDir: "{app}\decoders"; Excludes: "hfdl_compat.h,hfdl_win_shim\*,hfdl_win_shim"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
; ── Ícone da aplicação ────────────────────────────────────────────────────────
Source: "assets\app.ico"; DestDir: "{app}"; Flags: ignoreversion

; ── Visual C++ Redistributable (instala silenciosamente se necessário) ────────
Source: "{#BuildDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall skipifsourcedoesntexist

[Icons]
; Grupo no Menu Iniciar
Name: "{group}\{#MyAppName}";                        Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\app.ico"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}";  Filename: "{uninstallexe}"
; Atalho na Área de Trabalho (marcado por padrão na task "desktopicon")
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\app.ico"; Tasks: desktopicon

[Registry]
; Iniciar com o Windows (apenas se o usuário marcar a opção)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "{#MyAppName}"; \
    ValueData: """{app}\{#MyAppExeName}"""; \
    Flags: uninsdeletevalue; Tasks: autostart

; ── Limpa o serial salvo para que o usuario configure o PROPRIO dongle ───────
; Evita que instalacoes anteriores (ou do desenvolvedor) contaminem a config.
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

; Instalar VC++ Redist silenciosamente antes de lançar o app
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/quiet /norestart"; \
    StatusMsg: "Instalando Visual C++ Redistributable..."; \
    Flags: waituntilterminated

; Oferecer para abrir o RXSDR ao final da instalação
Filename: "{app}\{#MyAppExeName}"; Description: "Iniciar o {#MyAppName} agora"; \
    Flags: nowait postinstall skipifsilent

; Abrir página de download da API SDRplay (para usuários com hardware SDRplay)
Filename: "https://www.sdrplay.com/api/"; \
    Description: "Abrir a pagina da API SDRplay - so para quem tem RSP1/RSP1A/RSP1B/RSP2/RSPduo/RSPdx"; \
    Flags: shellexec postinstall skipifsilent unchecked

[Code]
procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then
  begin
    { O texto era longo demais e o Windows cortava o final. Como as caixas de
      marcar vem logo abaixo deste texto, a ultima linha visivel acabava sendo
      "Para hardware SDRplay (...):" - e ai parecia que "Iniciar o RXSDR agora"
      so servia para quem tem RSP. Agora o texto termina numa linha neutra que
      apresenta as caixas, e o detalhe do SDRplay ficou na propria caixa. }
    WizardForm.FinishedLabel.Caption :=
      'O {#MyAppName} foi instalado com sucesso!' + #13#10 + #13#10 +
      'RTL-SDR e RTL-TCP: pronto para usar. Vao junto os decodificadores' + #13#10 +
      'DMR, P25, NXDN, TETRA, ACARS, APRS, SITOR-B e DSC, mais 2765 memorias' + #13#10 +
      'com regua no espectro e filtro de quem esta no ar agora.' + #13#10 + #13#10 +
      'Quem usa SDRplay precisa da API oficial - veja a opcao abaixo.' + #13#10 + #13#10 +
      'Marque abaixo o que deseja fazer agora:';
  end;
end;
