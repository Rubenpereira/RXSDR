# RXSDR

Receptor SDR para Windows, escrito em C++ com Qt6. O programa roda como um
servidor local e a interface abre no navegador — o rádio em si não tem janela
própria, o painel é uma página web servida pelo próprio executável.

Feito por **PU1XTB — Ruben**, radioamador e radioescuta, em Araruama/RJ.

---

## O que ele faz

- Recepção em **AM, FM, NFM, WFM, USB, LSB e CW**
- **Espectro e cachoeira** com paleta Eclipse e média de espectros, o mesmo
  tratamento usado pelo OpenWebRX+ (o piso de ruído fica liso em vez de tremer)
- **S-meter analógico**, controle de ganho de RF, squelch, AGC e filtro de tom
- **Decodificadores digitais**: DMR, P25, NXDN, D-Star, TETRA, ACARS, APRS
  e PACKET
- Marcadores de banda, memórias e ajuste de sintonia de 1 Hz a 1 MHz
- Interface responsiva — funciona no navegador do PC, do celular e do tablet

## Hardware suportado

| Dispositivo | Observação |
|---|---|
| RTL-SDR (todos os modelos) | funciona direto, drivers inclusos |
| RTL-TCP | rádio remoto pela rede |
| SDRplay (RSP1/1A/1B/2/duo/dx) | exige a API oficial da SDRplay |

---

## Instalação no Windows

Baixe o instalador na aba **Releases** deste repositório. São duas versões:

- **RXSDR_Setup_x.x.xx.exe** — Windows 10 e 11 (build Qt6)
- **RXSDR_Setup_x.x.xx_Win7.exe** — Windows 7 SP1, 8, 10 e 11
  (executável com CRT estático, não precisa de `vcruntime.dll`)

Os dois já trazem todas as DLLs necessárias. Depois de instalar, abra o RXSDR
e o painel aparece no navegador.

Para hardware **SDRplay** é preciso instalar à parte a
[API oficial da SDRplay](https://www.sdrplay.com/api/) — ela não pode ser
redistribuída junto.

---

## Compilar a partir do código

Requisitos: Visual Studio 2019 ou superior (MSVC), CMake 3.16+ e Qt6
(Core, Gui, Widgets, Network, WebSockets, HttpServer, Svg).

```
COMPILAR.bat          build completo do Windows 10/11 (Qt6)
COMPILAR_WIN7.bat     build estático para Windows 7/8/10/11
ATUALIZAR_WEB.bat     só a interface (HTML/CSS/JS), sem recompilar
ABRIR.bat             abre o executável já compilado
GERAR_INSTALADOR.bat  gera o instalador (precisa do Inno Setup 6)
```

A interface fica em `project/web/index.html`, num arquivo único. O servidor
lê a pasta `web` que está **ao lado do executável em uso** — se alterar o HTML
e não vir mudança, confira qual `RXSDR.exe` você abriu.

### Decodificadores

Os decodificadores externos (DSDPlus, FMP24, Direwolf e outros)
**não estão neste repositório** — são programas de terceiros, com licenças
próprias, e precisam ser obtidos direto com seus autores. Os instaladores da
aba Releases já vêm com eles configurados.

---

## Licença

O RXSDR é distribuído sob a licença que está em [LICENSE.txt](LICENSE.txt):
uso, modificação e redistribuição livres para fins **não comerciais**, desde
que os créditos ao autor inicial sejam mantidos.

Partes do decodificador D-Star seguem a GPL v2 — veja
[LICENSE-DSD.txt](LICENSE-DSD.txt).

## Créditos

- `dstar_header.c/h` e `fcs.h` — Kristoff Bonne, ON1ARF
- `descramble.h` — Jonathan Naylor, G4KLX
- A cachoeira segue o tratamento e a paleta Eclipse do
  [OpenWebRX+](https://github.com/luarvique/openwebrx), de Marat Fayzullin,
  com o tema de Dimitar (LZ2DMV) e LZ4ZD

---

## Contato

**PU1XTB — Ruben** · Araruama, RJ, Brasil · pu1xtb@gmail.com
