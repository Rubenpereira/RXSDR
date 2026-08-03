# RXSDR — Documento de Arquitetura

**Versão:** 1.0 — Maio/2026
**Autor:** Ruben (PU1XTB)
**Plataforma:** Windows 10/11 (x64) — backend nativo, UI no navegador
**Stack:** C++17 / Qt6 (backend), HTML5 + Canvas/WebGL + JS (frontend)
**Hardware suportado:** RTL-SDR (USB), RTL-TCP (rede), SDRplay RSP1/RSP1A/RSP2/RSPdx/RSPduo

---

## Sumário

1. [Visão geral](#1-visão-geral)
2. [FASE 1 — Estrutura](#fase-1--estrutura)
3. [FASE 2 — UI Principal](#fase-2--ui-principal)
4. [FASE 3 — Fontes de Dados](#fase-3--fontes-de-dados)
5. [FASE 4 — DSP](#fase-4--dsp)
6. [FASE 5 — UI Completa e Decoders Digitais](#fase-5--ui-completa-e-decoders-digitais)
7. [Empacotamento e instalador](#empacotamento-e-instalador)
8. [Roadmap de implementação](#roadmap-de-implementação)

---

## 1. Visão geral

RXSDR é um software receptor SDR para Windows com **UI executada no navegador padrão do usuário**. O modelo arquitetural é cliente-servidor local:

```
+--------------------------------------+
|  Frontend (Chrome / Edge / Firefox)  |
|  - Espectro + Cachoeira (Canvas/WebGL)|
|  - VFO duplo, S-meter, controles     |
|  - Decoders (visualização de dados)  |
+----------------↑↓---------------------+
        HTTP REST  |  WebSocket binário
+----------------↑↓---------------------+
|  Backend RXSDR.exe (C++/Qt6)     |
|  - HTTP server (Qt Network)          |
|  - WebSocket server                  |
|  - Drivers SDR (RTL/RTL-TCP/SDRplay) |
|  - DSP (FFT, demod, decoders)        |
|  - Tray icon + auto-start browser    |
+--------------------------------------+
                  ↓
        Hardware USB / TCP
```

**Por que essa arquitetura:**
- O usuário usa **qualquer navegador moderno** (não precisa instalar Electron).
- A UI roda em `http://localhost:8080` e pode até ser acessada de **outro dispositivo na rede local** (tablet, celular, segundo PC).
- O DSP fica em C++ nativo → desempenho real-time para FFT, demodulação e decoders.
- Atualizações de UI são apenas arquivos estáticos servidos pelo backend, sem recompilar binário.

---

## FASE 1 — Estrutura

### 1.1 Árvore de diretórios

```
RXSDR/
├── CMakeLists.txt                  # Build raiz
├── README.md
├── LICENSE
├── installer/
│   ├── RXSDR.iss               # Script Inno Setup
│   └── assets/                     # Ícones, banner do instalador
│
├── src/                            # Código C++ backend
│   ├── main.cpp                    # Bootstrap: cria QApp, tray, server
│   ├── app/
│   │   ├── Application.{h,cpp}     # Orquestrador principal
│   │   ├── Config.{h,cpp}          # Persistência de settings (QSettings)
│   │   └── TrayController.{h,cpp}  # System tray + abrir navegador
│   │
│   ├── server/
│   │   ├── HttpServer.{h,cpp}      # Serve UI estática (web/)
│   │   ├── WsServer.{h,cpp}        # WebSocket: stream IQ / FFT / áudio
│   │   ├── RestApi.{h,cpp}         # Endpoints REST (tuning, modo, gain)
│   │   └── Protocol.{h,cpp}        # Formato binário (CBOR/MessagePack)
│   │
│   ├── sdr/
│   │   ├── ISdrDevice.h            # Interface comum (open, start, setFreq...)
│   │   ├── RtlSdrDevice.{h,cpp}    # via librtlsdr (USB)
│   │   ├── RtlTcpClient.{h,cpp}    # cliente TCP do protocolo rtl_tcp
│   │   ├── SdrplayDevice.{h,cpp}   # via SDRplay API v3
│   │   └── DeviceFactory.{h,cpp}   # Detecção e enumeração
│   │
│   ├── dsp/
│   │   ├── RingBuffer.h            # Buffer lock-free para IQ
│   │   ├── FftProcessor.{h,cpp}    # FFT (FFTW3) + janelas
│   │   ├── Waterfall.{h,cpp}       # Decimação temporal p/ cachoeira
│   │   ├── Demodulator.h           # Interface base
│   │   ├── DemodAM.{h,cpp}
│   │   ├── DemodFM.{h,cpp}         # WFM + NFM
│   │   ├── DemodSSB.{h,cpp}        # USB/LSB via Hilbert
│   │   ├── DemodCW.{h,cpp}         # BFO + filtro estreito
│   │   ├── Filters.{h,cpp}         # FIR/IIR, AGC, squelch
│   │   └── AudioOutput.{h,cpp}     # Qt Multimedia
│   │
│   ├── decoders/
│   │   ├── IDecoder.h
│   │   ├── DecoderADSB.{h,cpp}
│   │   ├── DecoderACARS.{h,cpp}
│   │   ├── DecoderAIS.{h,cpp}
│   │   ├── DecoderDMR.{h,cpp}
│   │   ├── DecoderRTTY.{h,cpp}
│   │   ├── DecoderSITOR.{h,cpp}
│   │   ├── DecoderCWMorse.{h,cpp}
│   │   ├── DecoderPACTOR.{h,cpp}
│   │   ├── DecoderPACKET.{h,cpp}   # AX.25
│   │   ├── DecoderSELCAL.{h,cpp}
│   │   ├── DecoderBPSK.{h,cpp}     # PSK31/63/125
│   │   ├── DecoderSONDE.{h,cpp}    # RS41, M10, DFM
│   │   └── DecoderHFDL.{h,cpp}
│   │
│   └── util/
│       ├── Logger.{h,cpp}
│       └── Timing.h
│
├── web/                            # Frontend estático
│   ├── index.html
│   ├── css/
│   │   ├── main.css
│   │   ├── vfo.css
│   │   └── decoders.css
│   ├── js/
│   │   ├── main.js                 # Bootstrap, roteamento
│   │   ├── api.js                  # Wrapper REST + WS
│   │   ├── spectrum.js             # Render Canvas/WebGL espectro
│   │   ├── waterfall.js            # Render cachoeira
│   │   ├── vfo.js                  # Painel VFO A/B
│   │   ├── smeter.js               # S-meter analógico SVG
│   │   ├── controls.js             # Modo, gain, squelch, AGC
│   │   ├── decoders.js             # UI da janela de decoders
│   │   └── theme.js                # Cores, palette espectro
│   └── assets/
│       ├── fonts/                  # Digital-7 (frequência LCD)
│       └── img/                    # Logo, ícones
│
├── third_party/                    # Libs externas (submodules ou cópia)
│   ├── librtlsdr/
│   ├── fftw3/
│   ├── sdrplay_api/                # SDK oficial SDRplay
│   └── qhttpengine/                # HTTP server p/ Qt (alternativa)
│
├── tests/                          # Testes unitários (Catch2 ou Qt Test)
│   ├── test_dsp.cpp
│   ├── test_decoders.cpp
│   └── ...
│
└── docs/
    ├── api.md                      # Documentação REST/WS
    ├── protocols.md                # Formato binário do stream
    └── build.md                    # Como compilar
```

### 1.2 Dependências externas

| Lib | Uso | Onde obter |
|---|---|---|
| **Qt 6.6+** | Qt Core, Qt Network, Qt WebSockets, Qt Multimedia | qt.io |
| **librtlsdr** | Driver USB RTL-SDR | github.com/librtlsdr/librtlsdr |
| **SDRplay API v3** | Driver SDRplay | sdrplay.com (login necessário) |
| **FFTW 3.3.10** | FFT otimizada | fftw.org |
| **liquid-dsp** (opcional) | Filtros, demoduladores prontos | liquidsdr.org |
| **mbelib** | Decodificação áudio DMR/P25 | github.com/szechyjs/mbelib |

### 1.3 Fluxo principal de inicialização

```
main()
  → criar QApplication
  → carregar Config (QSettings em %APPDATA%/RXSDR/config.ini)
  → instanciar Application
       → enumerar dispositivos SDR (DeviceFactory::scanAll())
       → iniciar HttpServer na porta 8080 (fallback 8081, 8082...)
       → iniciar WsServer na mesma porta (upgrade WebSocket)
       → criar TrayController (ícone na bandeja)
       → QDesktopServices::openUrl("http://localhost:8080")
  → app.exec()
```

### 1.4 Build (CMake)

```cmake
cmake_minimum_required(VERSION 3.21)
project(RXSDR VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS
    Core Network WebSockets Multimedia HttpServer)

find_package(FFTW3 REQUIRED)
find_library(RTLSDR_LIB rtlsdr REQUIRED)

add_executable(RXSDR WIN32
    src/main.cpp
    # ... lista completa de fontes
)

target_link_libraries(RXSDR PRIVATE
    Qt6::Core Qt6::Network Qt6::WebSockets
    Qt6::Multimedia Qt6::HttpServer
    FFTW3::fftw3f
    ${RTLSDR_LIB}
    # sdrplay_api importado como IMPORTED target
)

# Copia web/ para junto do .exe no install
install(DIRECTORY web/ DESTINATION bin/web)
install(TARGETS RXSDR DESTINATION bin)
```

---

## FASE 2 — UI Principal

A UI segue o estilo visual das imagens de referência: fundo escuro, espectro em verde fosforescente, cachoeira em paleta azul→ciano→amarelo→vermelho, painel VFO com tipografia LCD turquesa.

### 2.1 Layout geral (grid 12 colunas)

```
┌──────────────────────────────────────────────────────────────────┐
│  Menu Bar: Setup | Wave | Equalizer | CWX | Voice Messages |     │
│            Wizard | Compact screen | DX Cluster | XTRV |         │
│            Debug | About                                         │
├──────────────────────────────────────────────────────────────────┤
│  ┌──────┐  ┌──S-Meter──┐  ┌─VFO A────────────┬─Digi S-Meter─┐  ┌─Sliders─┐
│  │POWER │  │ analógico │  │ VFO A RX 10k NFM │ S9   90 RMS  │  │ Vol 20% │
│  ├──────┤  │   SVG     │  │ 145.570.000      │ [||||||....] │  │ Zoom 0x │
│  │BW|Stp│  └───────────┘  │ 2M NFM           │ 1 3 5 7 9 +20│  │ NR 0%   │
│  │><|AUT│                 └──────────────────┴──────────────┘  │ ...     │
│  └──────┘                                                      └─────────┘
├──────────────────────────────────────────────────────────────────┤
│  Mode: [LSB][USB][CW][AM][FM][NFM][WFM]                          │
│  Range [─●─] Contraste [─●─] Brilho [─●─] Speed [─●─]            │
│  [⚡ DECODER DIGITAL]                                            │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│              ESPECTRO (Canvas WebGL/2D) — verde                  │
│              ▲                                                   │
│              │ marker VFO A                                      │
│  -120 ─────────────────────────────────────────  freq           │
│                                                                  │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│              CACHOEIRA — paleta azul profundo→âmbar→laranja     │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

### 2.2 Componentes-chave

**Painel VFO e S-Meter Digital** (`web/js/vfo.js` e `web/js/smeter.js`)
- Frequência grande em fonte LCD (Digital-7) na cor `#00d4d4`.
- VFO A em destaque e S-Meter Digital (Barra de LEDs) acoplado ao lado direito.
- S-Meter Digital mostra leitura precisa em S-units e valor RMS, complementando o analógico.
- Modo (SSB, AM, FM, NFM, WFM, CW, USB, LSB) abaixo da frequência.

**S-Meter analógico** (`web/js/smeter.js`)
- SVG vetorial com agulha animada via `requestAnimationFrame`.
- Escala dupla: S0–S9+60dB.
- Integração com o Squelch: quando fechado, ponteiro descansa em S1; quando aberto, exibe valor real RMS ou dBm (HF).

**Bloco de Sliders 3x3**
- **Coluna 1**: Volume, Botão ÁUDIO (desbloqueia contexto de áudio do navegador).
- **Coluna 2**: Zoom, Filtro (BW), Squelch (SQL).
- **Coluna 3**: Audio AGC, Redutor de Ruído (NR) com gate espectral, Ganho de RF (0 a 100).

**Espectro** (`web/js/spectrum.js`)
- Canvas WebGL/2D para rendering fluido com FFT e Peak-Hold.
- Cor `#00ff66` (verde fosforescente) com preenchimento âmbar/verde em gradiente.
- Marker do VFO com "handle" acoplável para ajuste rápido de banda passante (BW).
- Click+arrasto = afinação via arraste no espectro.

**Cachoeira** (`web/js/waterfall.js`)
- Canvas 2D com `putImageData`.
- Paleta estilo Eclipse/RXSDR: fundo azul-marinho profundo, sinais médios em âmbar, fortes em laranja/vermelho. Evita tons brancos excessivos.
- Controles de Range, Contraste, Brilho e Speed no painel.

**Power / botões laterais**
- POWER liga/desliga RX.
- Botões de Bandwidth (BW) e Tuning Step (Stp) como dropdowns compactos.
- Botão "><" (Centralizar) e "AUTO" (Ajuste automático de paleta e range).

### 2.3 Paleta e tipografia

```css
:root {
  --bg-deep:      #0a0a0a;
  --bg-panel:     #141414;
  --bg-meter:     #1a1a1a;
  --grid:         #1f1f1f;
  --accent-lcd:   #00d4d4;
  --accent-tx:    #ff3030;
  --accent-rx:    #00ff66;
  --spectrum:     #00ff66;
  --text-dim:     #888;
  --text-bright:  #ddd;
  --border:       #2a2a2a;
}
@font-face {
  font-family: 'Digital-7';
  src: url('../assets/fonts/digital-7.ttf');
}
```

---

## FASE 3 — Fontes de Dados

Três drivers concretos implementam a interface `ISdrDevice`:

```cpp
class ISdrDevice {
public:
    virtual ~ISdrDevice() = default;
    virtual bool open(const QString& serial = "") = 0;
    virtual void close() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void setCenterFreq(uint64_t hz) = 0;
    virtual void setSampleRate(uint32_t sps) = 0;
    virtual void setGain(int tenthsDb) = 0;       // -10..490
    virtual void setBias(bool on) {}              // bias-T (RTL-SDR v3)
    virtual void setAntenna(const QString&) {}    // SDRplay RSPduo etc
    virtual QStringList listAntennas() const { return {}; }
    virtual uint32_t sampleRate() const = 0;

    // Callback assíncrono com bloco de amostras IQ (complex<float>)
    using SamplesCallback = std::function<void(const std::complex<float>*, size_t)>;
    virtual void setCallback(SamplesCallback cb) = 0;
};
```

### 3.1 RTL-SDR (USB)
- Wrapper sobre `librtlsdr` (`rtlsdr_open`, `rtlsdr_read_async`).
- Thread dedicada para o callback assíncrono → empurra blocos no `RingBuffer` lock-free.
- Sample rates: 240k, 960k, 1.024M, 1.4M, 1.8M, 2.048M, 2.4M, 2.56M, 2.88M.
- Range: 24 MHz – 1.766 GHz (com correção HF via Q-branch ou upconverter externo).
- Ganho em tenths-of-dB; opção AGC do tuner.

### 3.2 RTL-TCP (rede)
- Cliente do protocolo `rtl_tcp` (comandos de 5 bytes; stream de bytes uint8 I/Q).
- Conversão `uint8 → complex<float>` com `(x - 127.5f) / 127.5f`.
- Suporta conexão a qualquer servidor `rtl_tcp` (LAN ou WAN), reconexão automática.
- Útil para Raspberry Pi de campo + cliente Windows na sala.

### 3.3 SDRplay (RSP1A / RSP2 / RSPdx / RSPduo)
- Usa **SDRplay API v3** (instalação separada, exigência do fabricante).
- Range: 1 kHz – 2 GHz (cobertura completa HF/VHF/UHF).
- Bits: 14 (RSP1A) / 14 (RSPdx) → samples `int16` → conversão para `complex<float>`.
- Configurações específicas: LNA state (0–9), IF gain reduction, AGC bandwidth.
- Antenna port (A/B/C dependendo do modelo) exposto via `listAntennas()`.

### 3.4 Pipeline de dados

```
SDR Driver (thread driver) ──► RingBuffer<complex<float>>
                                       │
                          ┌────────────┼─────────────┐
                          ▼            ▼             ▼
                     FftProcessor  Demodulator   Decoder N
                          │            │             │
                          ▼            ▼             ▼
                    Bins p/ WS    PCM p/ áudio   Mensagens p/ WS
```

Todos consumidores leem o mesmo buffer em sub-threads via QThread. A taxa de FFT na UI é desacoplada do sample rate do SDR (decimação configurável).

### 3.5 Protocolo backend → frontend

**WebSocket binário** (`/ws`) com frames CBOR ou MessagePack:

```json
// Frame de espectro
{ "t": "fft", "ts": 1747500000, "n": 4096, "df": 488.28, "f0": 50000000,
  "data": [<int8 dB values, 4096 bytes>] }

// Frame de áudio (PCM 16-bit mono 12 kHz)
{ "t": "audio", "ts": ..., "data": <int16 samples> }

// Mensagem de decoder
{ "t": "dec", "decoder": "ADSB", "icao": "E48800", "callsign": "GLO1234",
  "lat": -23.5, "lon": -46.6, "alt": 35000 }
```

**REST** (`/api/*`) para comandos de controle:
- `POST /api/device/select` `{ "type": "rtlsdr", "serial": "00000001" }`
- `POST /api/tune` `{ "vfo": "A", "freq": 50135000, "mode": "USB" }`
- `POST /api/gain` `{ "value": 280 }`
- `GET  /api/status`
- `GET  /api/devices`

---

## FASE 4 — DSP

### 4.1 Pipeline DSP completo (RX)

```
IQ raw (Fs)
   │
   ├──► Decimação CIC + half-band → Fs/M (p/ banda do demod)
   │
   ├──► FFT 4096 pts (janela Blackman-Harris) → bins p/ espectro
   │                          │
   │                          └──► acumulação temporal → cachoeira
   │
   └──► NCO (mix com freq do VFO)
            │
            ▼
       FIR passa-banda do modo (LSB/USB/AM/FM/CW)
            │
            ▼
       Demodulador específico
            │
            ▼
       AGC + Squelch + Notch + NR
            │
            ▼
       Reamostragem p/ 48 kHz (Qt Multimedia)
            │
            ▼
       Audio sink
```

### 4.2 Detalhes por demodulador

**AM** (`DemodAM`)
- `out = sqrt(I² + Q²) − dc_offset`
- AGC com tempo de ataque 10 ms / decay 500 ms.

**FM** (`DemodFM`)
- Discriminação por arctangente diferencial: `phase[n] = atan2(Q,I); out = phase[n] − phase[n−1]`.
- Filtro de-ênfase 50µs (Europa) / 75µs (Américas) selecionável.
- NFM: largura 12.5 kHz; WFM: 200 kHz com stereo MPX (futuro).

**SSB (USB/LSB)** (`DemodSSB`)
- Filtro Hilbert FIR de 65 taps → cria sinal analítico.
- USB: `out = I · cos(ωt) − Q · sin(ωt)`; LSB: troca sinal.
- BFO ajustável via VFO.

**CW** (`DemodCW`)
- Igual SSB mas com filtro estreito (250–500 Hz) e BFO em 700 Hz.
- Pitch ajustável; AGC fast.

### 4.3 FFT e cachoeira

- FFTW3 single-precision, plan reutilizável.
- Janela: Blackman-Harris 4-term (boa relação SNR / lobo lateral).
- Overlap 50%, hop = N/2.
- Conversão para dB: `10 · log10(|X|² / N²)` com piso em −140 dBfs.
- Cachoeira: linha por linha empurrada para o buffer circular; envio incremental ao frontend.

### 4.4 Performance estimada

| Operação | CPU típica (i5-12400) |
|---|---|
| FFT 4096 @ 30 Hz | < 2% |
| Demod SSB 192 ksps | < 1% |
| ADSB @ 2.4 Msps | ~ 8% |
| ACARS 4 canais | ~ 5% |
| **Total operação completa** | < 25% |

---

## FASE 5 — UI Completa e Decoders Digitais

### 5.1 Janela "Decoder Digital"

Acionada pelo botão `[Decoder Digital]`. Abre overlay modal (full-screen ou janela) com **abas laterais**, uma por decoder:

```
┌─ Decoder Digital ─────────────────────────────────────────────[X]┐
│ ┌──ABAS───┬─────────────────────────────────────────────────────┐│
│ │ ADSB    │  TELA DO DECODER SELECIONADO                        ││
│ │ ACARS   │                                                     ││
│ │ AIS     │  Ex: ADSB →  Tabela de aeronaves +                  ││
│ │ DMR     │              mapa Leaflet com posições              ││
│ │ RTTY    │                                                     ││
│ │ SITOR-B │                                                     ││
│ │ CW      │                                                     ││
│ │ PACTOR  │                                                     ││
│ │ PACKET  │                                                     ││
│ │ SELCAL  │                                                     ││
│ │ BPSK    │                                                     ││
│ │ SONDE   │                                                     ││
│ │ HFDL    │                                                     ││
│ └─────────┴─────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────────────┘
```

### 5.2 Catálogo de decoders

| Decoder | Freq típica | Modulação | Implementação |
|---|---|---|---|
| **ADS-B** | 1090 MHz | PPM 1 Mbps | Detecção de preâmbulo + CRC Mode-S, decodificação CPR → lat/lon |
| **ACARS** | 131.45 / 131.55 MHz | AM-MSK 2400 bps | Demod MSK + bit sync + frame decode |
| **AIS** | 161.975 / 162.025 MHz | GMSK 9600 bps | NRZI + HDLC + AIVDM sentence |
| **DMR** | VHF/UHF | 4FSK 9600 bps | Sync detect + Viterbi + mbelib (áudio) |
| **RTTY** | HF (45/50/75 baud) | FSK | Mark/Space discriminator + Baudot decode |
| **SITOR-B** | HF MMSI | FSK 100 baud | CCIR-476 FEC |
| **CW (Morse)** | qualquer | OOK | Detecção de envelope + WPM auto + decoder gambit |
| **PACTOR** | HF | M-FSK | Pactor-1 (FSK 100/200 baud) — Pactor-2/3 são proprietários |
| **PACKET** | VHF/HF | AFSK/FSK | AX.25 com Bell-202 (1200) ou G3RUH (9600) |
| **SELCAL** | HF/VHF aviação | 2-tone | Detecção de pares de tons (lista ARINC) |
| **BPSK (PSK31/63/125)** | HF | DBPSK | Costas loop + Varicode |
| **Radiossondas** | 400–406 MHz | GFSK | RS41, M10, DFM-09 → telemetria + GPS |
| **HFDL** | HF aviação | M-PSK 300–1800 bps | Squitter detect + frame decode |

### 5.3 Arquitetura comum dos decoders

```cpp
class IDecoder {
public:
    virtual ~IDecoder() = default;
    virtual QString name() const = 0;
    virtual void feed(const std::complex<float>* iq, size_t n) = 0;
    virtual void setSampleRate(uint32_t sps) = 0;

    // Sinal emitido com a mensagem decodificada (JSON)
    using MessageCallback = std::function<void(const QJsonObject&)>;
    void setOnMessage(MessageCallback cb) { onMsg_ = cb; }
protected:
    MessageCallback onMsg_;
};
```

Cada decoder roda em sua própria QThread, recebendo IQ pré-decimado da banda apropriada. As mensagens entram em uma fila e são empurradas via WebSocket para a UI.

### 5.4 Tela específica de cada decoder (frontend)

- **ADS-B / AIS:** tabela de contatos + mapa Leaflet (tiles offline opcionais).
- **ACARS / HFDL:** log de mensagens com filtro por callsign/aeroporto.
- **DMR:** lista de talkgroups ativos + indicador de áudio.
- **RTTY / BPSK / SITOR / CW / Packet:** terminal de texto cumulativo + waterfall estreita.
- **Sondes:** tabela com identificador, lat/lon, altitude, taxa de subida, temperatura.
- **SELCAL:** alerta sonoro quando código configurado pelo usuário é detectado.

### 5.5 Interação UI completa

- Sintonia rápida com **memórias**: arquivo `memories.json` em `%APPDATA%/RXSDR/`.
- **DX Cluster**: conexão Telnet a clusters (DXSpider/AR-Cluster) e marcadores no espectro.
- **Wizard de primeira execução**: detecta hardware, sugere ganho/sample rate por banda.
- **Compact screen**: layout reduzido para monitor secundário.
- **Equalizer**: 5 bandas para áudio (low-cut, presence, etc).
- **Voice Messages / CWX**: futuro (TX), placeholders no menu por enquanto.
- **Setup**: tema, paleta cachoeira, hotkeys, atalhos.

---

## Empacotamento e instalador

1. Build em **Release** com Qt 6.6 MSVC2022.
2. `windeployqt RXSDR.exe` → coleta DLLs Qt necessárias.
3. Copiar `web/`, ícone tray, libusb-1.0.dll, librtlsdr.dll.
4. SDRplay API: o instalador detecta se a API v3 está presente; senão, mostra link para download.
5. **Inno Setup** (`installer/RXSDR.iss`) gera `RXSDR_Setup_1.0.exe`:
   - Atalho no menu Iniciar e Desktop.
   - Auto-start opcional (registro `HKCU\...\Run`).
   - Associação `.masdr` para arquivos de memória.
   - Desinstalador automático.

---

## Roadmap de implementação

| Sprint | Entrega | Duração estimada |
|---|---|---|
| **1** | Estrutura CMake + Qt + tray + servidor HTTP servindo `index.html` placeholder | 1 semana |
| **2** | Driver RTL-SDR + WebSocket de FFT bruta + UI espectro/cachoeira | 2 semanas |
| **3** | VFO + S-meter + controles de modo + demoduladores AM/FM/SSB/CW + áudio | 2 semanas |
| **4** | Driver RTL-TCP + SDRplay + persistência de configurações | 1 semana |
| **5** | Decoders fase 1: ADS-B, AIS, RTTY, CW, BPSK | 2 semanas |
| **6** | Decoders fase 2: ACARS, DMR, Packet, SELCAL | 2 semanas |
| **7** | Decoders fase 3: Sondes, HFDL, SITOR, Pactor-1 | 2 semanas |
| **8** | Polimento, testes, instalador Inno Setup | 1 semana |

**Total: ~13 semanas** para a primeira versão completa estável.

---

## Anexos
- Estrutura inicial de pastas: `src/`, `web/`, `CMakeLists.txt` (gerados separadamente)
- Documentação de API REST/WS: ver `docs/api.md` quando o backend começar
