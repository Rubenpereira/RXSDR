# Histórico de mudanças

## 1.0.46

- **Botão AUTO da cachoeira agora reproduz o seu padrão em qualquer banda.**
  Antes ele media o espectro do momento e devolvia um resultado diferente a
  cada clique, conforme o ruído e os sinais do instante. Agora ele mede o piso
  de ruído da banda em que você está e recalcula Range e Brilho para que a
  imagem fique com a **mesma aparência** do padrão guardado — os números nos
  controles serão outros, e é isso mesmo que se quer, porque cada banda tem um
  piso de ruído diferente. Em 40 m a cor fica boa e em 20 m a mesma escala
  clareia; agora não clareia mais.
- **Botão PADRÃO**, ao lado do AUTO. Deixe a cachoeira do jeito que você gosta
  e clique uma vez: aquela aparência passa a ser o alvo do AUTO em todas as
  bandas. O que se guarda é a *relação* entre o piso de ruído e a escala de
  cores, não os números — por isso funciona onde o ruído é outro.
- A cachoeira abre com Range 53 e Brilho 106, e o AUTO leva ao mesmo lugar:
  abrir o programa e clicar no AUTO passam a ter o mesmo resultado.
- **Correção importante: abrir um decodificador podia derrubar a conexão com o
  rádio.** A tela mostrava o rádio desligado, mas ele continuava ligado e
  sintonizado — o que caía era a ligação entre o navegador e o programa. Bastava
  o decodificador escrever uma linha de texto na tela. Afetava os doze:
  SITOR-B, DSC, CW, APRS, ACARS, AIS, TETRA, P25, Pactor, SELCAL, o analisador
  de sinal desconhecido e o DMR.

## 1.0.41

- **Idioma português e inglês**, com botão no cabeçalho. Na primeira abertura
  o programa pergunta e memoriza a escolha. O motor nunca traduz conteúdo
  recebido — texto decodificado, nomes de memória e o VFO ficam intactos.
- A cachoeira **abre** com Range 50 e Brilho 106, valores ajustados no ar. O
  botão AUTO continua medindo o espectro do momento, como antes.

## 1.0.40

- **Memórias** com régua no topo do espectro, no formato do OpenWebRX. Criar a
  partir do VFO (perguntando a categoria), editar, excluir, importar e
  exportar. O instalador traz 2765 frequências: as suas mais 1807 emissoras
  importadas da grade do EiBi.
- **Filtros da régua**: mostrar todas, somente utilitárias, somente broadcast,
  e — o mais útil — **broadcast no ar agora**, que usa o relógio UTC e a grade
  de horários para mostrar só o que está transmitindo neste momento. De 1807
  emissoras para cerca de 300 a 500 conforme a hora.
- A classificação entre utilitária e radiodifusão é feita pelas **faixas da
  UIT** e pela regra "AM abaixo de 30 MHz é radiodifusão" — que mantém as
  torres de controle aéreo, também em AM mas acima de 118 MHz, entre as
  utilitárias.
- A linha `Span | Ref | Avg | FFT` saiu do topo do espectro para o rodapé,
  abrindo espaço para a régua.
- **Sombra da banda passante muda de lado conforme o modo**: em USB fica acima
  do risco, em LSB abaixo, como no SDR#. O risco continua marcando a
  frequência sintonizada.
- **Suavização do espectro corrigida.** Ela era aplicada a cada desenho (30 por
  segundo) sobre dados que chegam a 15, e era assimétrica — subida 0,55 contra
  descida 0,12. O traço colava no pico e descia em degraus, dando a impressão
  de espectro travado. Pior: o comportamento mudava sozinho se o navegador
  caísse de 30 para 20 quadros. Agora as constantes são tempos (40 ms para
  subir, 130 para descer) e o resultado é o mesmo em qualquer taxa de quadros.
- Largura padrão: SSB 2,4 kHz, AM 6 kHz, e 1,8 kHz acrescentado à lista.
- Teclado de frequência responde ao toque, sem a espera do duplo-toque.

## 1.0.37

### Novidades

**SITOR-B / NAVTEX nativo.** Decodificador escrito do zero em C++, sem
programa externo. Lê os boletins das estações costeiras — avisos aos
navegantes, faróis apagados, boias garreadas — com coordenadas e datas.

**DSC — chamada seletiva digital (ITU-R M.493).** Também nativo. Interpreta o
formato, o MMSI de quem chama e de quem é chamado, a categoria e o fim de
sequência, e confere a soma de verificação (ECC) da ITU. Validado em gravações
de tráfego GMDSS real em 8414,5 kHz, com o ECC fechando em todas as mensagens.

**Analisador de sinal desconhecido.** Aponte o rádio para um sinal digital que
você não reconhece e ele mede: quantos tons, a separação entre eles, a
velocidade, e diz com que modos aquilo é compatível — inclusive lembrando as
frequências que separam SITOR-B de DSC, já que os dois são idênticos no ar.

**Abrir arquivo de áudio nos decodificadores.** MP3, WAV ou OGG. O áudio toca
acompanhando o texto, como se viesse do rádio, com pausa, continuação e
reinício. Serve para decodificar gravações antigas e para testar parâmetros
sem depender da propagação.

**Memórias com régua.** No formato do OpenWebRX (`bookmarks.json`), ao lado do
executável. A régua fica na faixa acima do espectro, fora da cachoeira, para
não cobrir os sinais. Criar a partir do VFO, editar, excluir, importar e
exportar. O instalador traz 958 frequências e não sobrescreve as suas ao
atualizar.

**Botão BW 500** nos painéis do SITOR-B e do DSC. Estreita a recepção para
500 Hz — o sinal ocupa uns 300, então receber com 3 kHz joga fora cerca de
10 dB. Um segundo clique devolve a largura anterior.

**Campos de tom central, shift e baud** nos decodificadores, com o valor
guardado entre sessões. Nem toda estação segue a norma: a Marinha argentina em
12578 kHz transmite com 200 Hz de shift, e não com os 170 do padrão. O canal
12578 já entra na lista com o shift certo.

### Correções

**Ordem dos bits do CCIR 476.** O SITOR-B montava os bits do mais para o menos
significativo. A norma manda o contrário. Como a inversão *preserva* o peso 4
do código, a verificação continuava aprovando e o texto saía consistente porém
ilegível — uma métrica cega justamente para o defeito que existia.

**Tabela CCIR 476.** A anterior tinha sido montada de memória e estava errada:
31 códigos em vez de 32, faltando `0x3A` e `0x1D`, que sozinhos são quase um
quarto do tráfego real. Refeita a partir do ARRL Handbook e conferida
caractere a caractere.

**Duplicação de dois caracteres** (`APAPAGADA`, `CONFIABLELE`, `NARARANJA`).
O bloco de áudio juntava 200 bits e era descartado inteiro, mas 200 não é
múltiplo de 7 — cada emenda jogava fora um pedaço de caractere. Perder um
número ímpar de caracteres inverte a paridade das posições, e como DX e RX
alternam, o decodificador passava a casar cada DX com o RX errado, reemitindo
o que já tinha saído. Agora só é consumido o áudio que fechou caractere
inteiro. Num boletim real, as duplicações caíram de 43 para 1.

**Polaridade do DSC.** Na M.493 o bit 1 é o tom mais *baixo* — 1615 Hz contra
1785 Hz na sintonia nominal. Estava invertido, o que obrigava a marcar
"Inverter" na tela. A convenção certa foi para dentro do núcleo e a caixa
voltou a ficar desmarcada.

**Analisador: prova de alternância.** Ela comparava as duas envoltórias
diretamente, mas em onda curta o desvanecimento levanta e abaixa os dois tons
ao mesmo tempo, e esse movimento comum dava correlação positiva mesmo numa FSK
perfeita. Agora a comparação é feita na diferença normalizada, que cancela o
desvanecimento.

**Analisador: filtros largos demais.** Tinham 400 Hz de largura, mais que o
dobro dos 170 Hz do SITOR-B, então cada filtro escutava os dois tons e a
alternância sumia por construção. A largura passou a acompanhar o
deslocamento medido.

**Analisador: veredito de "cifrado".** Ele afirmava que o conteúdo era cifrado
quando os bits pareciam aleatórios. Só que o teste lê os bits no ritmo da
velocidade medida, e se essa medida erra, a leitura sai fora de compasso e
produz exatamente a mesma assinatura. Aconteceu com um NAVTEX em texto claro
que estava sendo decodificado na tela. Agora ele apresenta as duas
explicações em vez de escolher uma.

**Áudio contínuo ao girar o VFO.** Toda sintonia silenciava o áudio por 250 ms
para esconder o estalo do dongle ao trocar de frequência central. Só que isso
era aplicado sempre, inclusive nos passos de 1 Hz, que nem chegam ao hardware.
Agora o silenciamento só ocorre quando o dongle realmente se move.

**Largura de banda padrão.** SSB passou de 3,0 para **2,4 kHz** e AM de 10 para
**6 kHz**. A lista ganhou **1,8 kHz** para SSB estreito.

### Outros

- Ícone novo, embutido no executável e no assistente de instalação
- Botão de energia mostra **ON** em verde e **OFF** em vermelho
- Modo Q com três estados: Off, On e Auto, trocando sozinho em 24 MHz
- Frequências da Marinha do Brasil (Rio) na lista do SITOR-B, com os horários
  em UTC
- A linha `Span | Ref | Avg | FFT` saiu do topo do espectro e foi para o
  rodapé, abrindo espaço para a régua de memórias

### O que segue em aberto

- A medição de **velocidade** do analisador erra em sinal com estrutura de
  caractere forte: num NAVTEX de 100 baud ela aponta 28, que é o dobro da taxa
  de caracteres. O analisador avisa quando o número não bate com nenhum modo
  conhecido, em vez de apresentá-lo como certo.
- O **DSC ainda não foi confirmado ao vivo** — funcionou em gravações, falta
  receber uma chamada no ar.
