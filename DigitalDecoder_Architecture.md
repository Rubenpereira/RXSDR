# RXSDR — Arquitetura do Subsistema Digital Decoder
**Versão**: 1.0.13 
**Data**: 2026-05-28  
**Autor**: PU1XTB — Ruben  

---

## 1. Visão Geral

O subsistema Digital Decoder do RXSDR integra decodificadores digitais e utilitários de recepção diretamente no fluxo de áudio demodulado. A detecção de voz e o controle de silenciamento no frontend são feitos pelo motor **VOX Adaptativo** em JavaScript, enquanto a decodificação de voz de protocolos digitais (como DMR) é realizada externamente pelo **DSD-FME**, controlado por uma classe gerenciadora em C++ (`DsdManager`).

Todos os decoders (DMR/DSD, ACARS, RTTY, APRS, PACKET, ADSB, SITOR-B, CW MORSE, PACTOR, SELCALL, BPSK e SONDES) usam a função `feedAudio()`, recebendo áudio PCM demodulado (USB, LSB ou NFM) diretamente do motor de DSP do RXSDR.

Alguns decoders são baseados em processos nativos ou wrappers externos gerenciados por classes dedicadas C++ (Managers) que iniciam o subprocesso, enviam o áudio PCM via `stdin` ou canais de áudio locais, e capturam o texto decodificado via `stdout` e o áudio decodificado via conexões de rede locais (como portas UDP).

```
┌─────────────────────────────────────────────────────────────────┐
│  Browser (index.html)                                           │
│                                                                 │
│  ┌──────────────────┐    PCM via WS    ┌───────────────────┐   │
│  │  DigitalDecoder  │◄─────────────────│  Backend (WS)     │   │
│  │  (JS module)     │                  │  /api/ws/audio    │   │
│  │  • VOX adaptativo│                  └───────────────────┘   │
│  │  • Audio gate    │                                           │
│  │  • Painel UI     │                                           │
│  └──────────────────┘                                           │
└─────────────────────────────────────────────────────────────────┘
           │ REST API & WebSockets
           ▼
┌─────────────────────────────────────────────────────────────────┐
│  Qt6 Backend (C++)                                              │
│                                                                 │
│  Application.cpp ──► DsdManager.cpp (DMR / DSD-FME)             │
│                           │                                     │
│                           │ QProcess (stdin: PCM / UDP: Voz)    │
│                           ▼                                     │
│                     dsd-fme.exe (Processo Filho)                │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. Decodificação DMR (DSD-FME)

O decodificador DMR é gerenciado pela classe `DsdManager` (`src/decoders/DsdManager.cpp`).

- **Executável**: `dsd-fme.exe` rodando como subprocesso controlado pelo `QProcess`.
- **Entrada de Áudio**: O áudio demodulado do RXSDR (tipicamente em modo NFM ou FM largo) é enviado para o decoder através de `feedAudio()`, que realiza o reamostragem interno se necessário e o entrega ao processo via pipe `stdin`.
- **Saída de Voz Decodificada**: O áudio digital de voz decodificado pelo DSD-FME é transmitido de volta para o RXSDR via pacotes UDP em porta local (geralmente formatado a 16000 Hz) e coletado pelo socket `QUdpSocket` no `DsdManager`, que reconverte para a taxa de amostragem de saída do sistema (48000 Hz) para ser reproduzido.
- **Polaridade**: Suporta inversão dinâmica de polaridade (passando a flag `-P` ao inicializar o executável) para sincronização e decodificação correta dependendo do hardware discriminador.

---

## 3. Frontend JavaScript — Módulo `DigitalDecoder`

Localização: `web/index.html`, bloco `DIGITAL DECODER — Painel DMR / P25 / NXDN / TETRA`

Implementado como IIFE (módulo isolado), exposto como `window.DigitalDecoder`.

### 3.1 Estado interno

```js
let proto       = 'dmr';  // protocolo ativo
let ts          = 1;       // time slot (DMR/TETRA/P25P2)
let colorCode   = 1;       // DMR Color Code (0-15)
let nac         = 0x293;   // P25 NAC
let ran         = 0;       // NXDN RAN
let tgFilter    = '';      // filtro de TalkGroup
let autoNFM     = true;    // aplica NFM + BW automaticamente
let muteSDR     = true;    // sempre true (silencia SDR)
let voxGate     = true;    // sempre true (só abre com voz)
let _savedSql   = null;    // valor do squelch salvo antes de abrir o painel
```

### 3.2 Flags globais de áudio

| Flag | Tipo | Efeito |
|------|------|--------|
| `window._digitalMuted` | boolean | `Backend.playPcm` e `AudioOut.playPcm` retornam sem enviar áudio |
| `window._digitalDecoding` | boolean | Ignora verificação do slider de squelch RF (SQL) — evita silêncio entre bursts digitais |

**Onde são verificadas (`Backend.playPcm`):**
```js
if (window._digitalMuted) return;
if (!window._digitalDecoding) {
  const sql = Number(document.getElementById('sqlSlider')?.value ?? -90);
  if (sql > -90 && Smeter.rfLive && Smeter.peakDb < sql) return;
}
```

---

## 4. Motor VOX Adaptativo

### 4.1 Algoritmo

O VOX estima continuamente o **noise floor** do canal em idle e usa um limiar dinâmico para abrir somente com voz real.

```
RMS de entrada (PCM 16-bit):
  rms = sqrt( sum(s²/32768²) / N )

Estimativa do noise floor (EMA lenta):
  se VOX fechado:
    _noiseFloor = NF_ALPHA × rms + (1 - NF_ALPHA) × _noiseFloor
    _noiseFloor = max(_noiseFloor, NF_MIN)

Limiar dinâmico:
  dynThresh = _noiseFloor × _voxK

VOX abre quando:  rms > dynThresh
VOX fecha após:   VOX_HOLD_MS sem sinal acima do limiar
```

### 4.2 Constantes e faixas

| Constante | Valor padrão | Faixa configurável | Descrição |
|-----------|-------------|-------------------|-----------|
| `NF_ALPHA` | `0.002` | fixo | Velocidade de aprendizado do noise floor |
| `NF_MIN` | `0.0004` | fixo | Noise floor mínimo (evita div-by-zero) |
| `_voxK` | `5.0` | 2–20 (slider Sensib.) | Multiplicador sobre o noise floor |
| `VOX_HOLD_MS` | `600 ms` | 100–2000ms (slider Hold) | Tempo mínimo de abertura do VOX |

**Mapeamento do slider Sensib. (1-50 → K 20-2):**
```js
_voxK = 20 - (val - 1) * (18 / 49);
// Slider na direita = mais sensível = K menor = limiar mais baixo
```

### 4.3 Controle do gain node Web Audio

O Web Audio API pre-agenda `AudioBufferSource` nodes até 400ms no futuro. A flag `_digitalMuted` impede NOVOS frames, mas não para os já agendados. Por isso o controle é feito diretamente no gain node:

| Evento | Ação no gain node |
|--------|-------------------|
| `DigitalDecoder.init()` | `gain.setValueAtTime(0, ctx.currentTime)` — silencia TUDO imediatamente |
| `_onVoxOpen()` | `gain.setValueAtTime(AudioOut.volume(), ctx.currentTime)` — abre áudio |
| VOX fecha (em `_updateVoxUI`) | `gain.setValueAtTime(0, ctx.currentTime)` — silencia tail buffered |
| `DigitalDecoder.onClose()` | `gain.setValueAtTime(AudioOut.volume(), ctx.currentTime)` — restaura volume normal |

### 4.4 Controle automático do Squelch RF

Ao abrir o painel, o squelch de RF **deve ser forçado para `-90 dB` (totalmente aberto)**, pois o silêncio é responsabilidade exclusiva do VOX adaptativo — não do squelch de RF. O squelch fechado bloquearia o fluxo de PCM para o VOX, impedindo a detecção de voz.

**Comportamento em `init()`:**
```js
const sqlEl  = document.getElementById('sqlSlider');
const sqlVal = document.getElementById('sqlVal');
if (sqlEl) {
  _savedSql = sqlEl.value;           // salva valor anterior do usuário
  sqlEl.value = -90;
  sqlEl.dispatchEvent(new Event('input'));
}
if (sqlVal) sqlVal.textContent = '-90';
```

**Comportamento em `onClose()`:**
```js
if (_savedSql !== null) {
  const sqlEl  = document.getElementById('sqlSlider');
  const sqlVal = document.getElementById('sqlVal');
  if (sqlEl) {
    sqlEl.value = _savedSql;
    sqlEl.dispatchEvent(new Event('input'));
  }
  if (sqlVal) sqlVal.textContent = String(_savedSql);
  _savedSql = null;
}
```

---

## 5. Tabela de Bandwidth por Protocolo

```js
const PROTO_BW = {
  dmr   : 12500,   // DMR Tier II/III
  p25   : 12500,   // P25 Phase 1
  p25p2 : 6250,    // P25 Phase 2
  nxdn12: 6250,    // NXDN 6.25 kHz
  nxdn25: 12500,   // NXDN 12.5 kHz
  tetra : 25000,   // TETRA
  dpmr  : 6250,    // dPMR
  dstar : 12500,   // D-STAR
  ysf   : 12500,   // YSF (C4FM)
  moto2 : 12500,   // Motorola Type II
  edacs : 25000,   // EDACS
};
```

Quando **Auto NFM+BW** está ativo, `applyAutoNFM()` chama:
```js
Backend.tune(currentFreqHz, 'NFM', PROTO_BW[proto])
```

---

## 6. Painel de Controle Web (`DigitalDecoder.buildPanelHTML`)

O painel é gerado dinamicamente por `buildPanelHTML()` e injetado como `decoder-panel dp-digital` (460px, tema verde).

### 6.1 Controles disponíveis

| Controle | ID HTML | Protocolo | Descrição |
|----------|---------|-----------|-----------|
| Seletor de protocolo | `dig-proto` | Todos | `<select>` com 11 opções |
| TS1 / TS2 | `dig-ts1`, `dig-ts2` | DMR, TETRA, P25P2 | Toggle time slot |
| Color Code | `dig-cc` | DMR | Input 0-15 |
| NAC | `dig-nac` | P25 Ph.1/2 | Input hex |
| RAN | `dig-ran` | NXDN | Input 0-63 |
| Filtro TG | `dig-tg` | Todos | Input text |
| Auto NFM+BW | `dig-auto-nfm` | Todos | Checkbox |
| Sensib. (K) | `dig-vox-thresh` | Todos | Slider 1-50 |
| Hold | `dig-vox-hold` | Todos | Slider 1-20 |

### 6.2 Status bar (IDs)

| ID | Conteúdo |
|----|---------|
| `dig-s-proto` | Nome do protocolo ativo |
| `dig-s-ts` | TS1/TS2 (oculto em protocolos FDMA) |
| `dig-s-cc` | Color Code ativo |
| `dig-s-call` | Bloco de chamada ativa (oculto em idle) |
| `dig-s-idle` | Texto "AGUARDANDO..." (visível em idle) |
| `dig-s-tg` | TalkGroup da chamada atual |
| `dig-s-src` | ID da fonte (radio ID) |

### 6.3 Indicadores VOX

| Elemento | ID | Descrição |
|----------|----|-----------|
| LED VOX | `dig-vox-led` | Verde = aberto, Laranja = sinal detectado mas abaixo do hold |
| Label VOX | `dig-vox-lbl` | Texto "VOX" / "VOX ●" |
| Barra RMS | `dig-vox-bar` | Energia RMS atual (escala logarítmica) |
| Linha threshold | `dig-vox-thresh-line` | Posição dinâmica = dynThresh |
| NF display | `dig-floor-lbl` | Noise floor em dBFS |

---

## 7. Fluxo de Dados Completo

```
RTL-SDR USB / SDRplay
    │
    ▼
Demodulator.cpp   ──► AudioOutput.cpp ──► WsServer.cpp ──► browser /ws/audio
                                                │
                              PCM Int16 frames (512 samples @ 48kHz)
                                                │
                                                ▼
                                     Backend.playPcm() [JS]
                                          │
                                          ├─ DigitalDecoder.onRms(rms)  ← VOX input
                                          │
                                          ├─ if (_digitalMuted) return  ← gate 1 (flag JS)
                                          │
                                          ├─ if (!_digitalDecoding)     ← gate 2
                                          │    check SQL slider          (SQL=-90 forçado,
                                          │                               nunca bloqueia)
                                          └─ AudioOut.playPcm(pcm)
                                               │
                                                AudioOut.gain          ← gate 3 (gain node)
                                                    0.0  → silêncio (VOX fechado)
                                                    vol  → áudio (VOX aberto)

NOTA: SQL slider é forçado para -90 dB em init() e restaurado em onClose().
      Isso garante que o PCM sempre chegue ao VOX independente da configuração
      do squelch que o usuário tinha antes de abrir o painel.