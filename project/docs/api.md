# RXSDR — API REST e WebSocket

Backend escuta em `http://localhost:8080` (HTTP/REST + arquivos estáticos)
e `ws://localhost:8081/ws` (stream binário).

## REST endpoints

### GET /api/devices
Lista todos os hardwares detectados.
```json
[
  { "type":"rtlsdr", "serial":"00000001", "name":"R820T2" },
  { "type":"sdrplay","serial":"190423A2", "name":"RSP1A"   },
  { "type":"rtltcp", "host":"192.168.1.50","port":1234 }
]
```

### POST /api/device/select
Abre um dispositivo específico.
```json
{ "type":"rtlsdr", "serial":"00000001" }
```

### POST /api/tune
Sintoniza VFO A ou B.
```json
{ "vfo":"A", "freq":14250000, "mode":"USB" }
```

### POST /api/gain
Ganho em décimos de dB (-1 = AGC automática).
```json
{ "value":280 }
```

### GET /api/status
Estado completo.
```json
{
  "device": { "type":"rtlsdr", "name":"R820T2" },
  "vfoA": { "freq":14250000, "mode":"USB" },
  "vfoB": { "freq":14283000, "mode":"USB" },
  "sampleRate": 2048000,
  "gain": 280,
  "running": true,
  "cpu": 14.2
}
```

## WebSocket

Frames binários e JSON misturados.

### Frame FFT (binário)
```
byte 0      : 0x01     (tipo = fft)
bytes 1..N  : N valores int8 em dBfs (já comprimidos para envio)
```

### Frame Áudio (binário)
```
byte 0      : 0x02     (tipo = audio PCM s16le 12 kHz mono)
bytes 1..   : amostras int16
```

### Mensagem de decoder (JSON)
```json
{ "t":"dec", "decoder":"ADSB",
  "icao":"E48800", "callsign":"GLO1234",
  "lat":-23.5, "lon":-46.6, "alt":35000 }
```

### Mensagem de status periódica (JSON)
```json
{ "t":"status", "cpu":13.4, "rate":2048000, "overrun":false }
```
