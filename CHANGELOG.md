# Histórico de mudanças

## 1.0.67

Nova Versão RXSDR v1.0.67

Corrigido: o TETRA nunca abria a voz no Windows. O programa que lê o sinal (tetra-rx) não conseguia mandar os dados da célula para o RXSDR, porque faltava ligar a rede do Windows antes de abrir a conexão (WSAStartup). Sem esses dados não chegavam as informações da célula, as chamadas nem a voz. O tetra-rx foi recompilado com a correção, a mesma que já estava no RXSDR Nativo.

RXSDR Nativo 1.0.1: novo decodificador DRM (Digital Radio Mondiale), o rádio digital das ondas curtas. Usa o Dream, o receptor DRM de código aberto, com áudio AAC e xHE-AAC. Escolha DRM na janela dos decodificadores e uma emissora da lista (a grade de ondas curtas do drmrx.org; as que estão no ar na hora aparecem em verde). O rádio vai para AM com 10 kHz só para marcar o canal; o áudio decodificado toca no lugar do áudio do rádio. A janela mostra o nome da emissora, país, idioma, o texto que ela transmite, SNR, modo, QAM e as luzes de sincronismo.

RXSDR Nativo 1.0.1: os sliders ganharam o desenho de fader de mesa de som, deitados como antes. O botão fica vermelho ao arrastar ou ao girar a rodinha do mouse, e os ajustes Range, Brilho e Speed da cachoeira ficaram mais compridos. O pacote agora traz as licenças (LICENSE.txt e TERCEIROS.txt, com a lista dos programas de outros autores).

## 1.0.66

Nova Versão RXSDR v1.0.66

Incluído o APRS de HF, a 300 baud, no mesmo painel do APRS.

No alto do painel há agora a escolha da banda: VHF 1200 baud (145.570 MHz, como sempre foi) ou HF 300 baud. Em HF, o rádio vai sozinho para USB no canal escolhido — 30 m em 10.147,6 kHz, o mais usado, ou 20 m em 14.102,3 kHz — e, ao fechar a janela, volta exatamente como estava: frequência, modo, largura, passo e squelch. O painel lembra a última banda e o último canal usados.

Em HF a sintonia nunca é exata, e a 300 baud os dois tons ficam a só 200 Hz um do outro. Por isso o decodificador roda sete vezes em paralelo, espalhado em volta do tom. Em teste com 100 pacotes e a sintonia 100 Hz fora do ponto, ele leu 65, contra 15 do jeito comum.

Incluído o decodificador de ALE (Automatic Link Establishment), no menu Digital Decoder.

Decodifica o ALE de segunda geração, o 2G ALE, que é o mesmo sinal nas normas MIL-STD-188-141A, MIL-STD-188-141B e FED-STD-1045. É o ALE que se ouve na rede de radioamadores (HFN) e em muitas estações utilitárias. Cada chamada aparece numa linha, com a hora UTC: para quem é ([TO]), quem chama ([TIS]), fim do enlace ([TWAS]), mensagens de texto ([CMD AMD]) e qualidade do canal ([LQA]). Chamadas de varredura repetidas aparecem resumidas, como "[TO][@@?] x39".

Cada chamada também ganha uma frase em português, no estilo do MultiPSK: "Chamada de 00012 para 21011", "Sondagem de 21011", "Fim de enlace", com a qualidade informada (LQA) e o texto das mensagens. A lista de canais traz, além da rede HFN, 132 canais de ALE do Brasil (Marinha, Forças Armadas, Força Aérea), vindos da lista ILGRADIO de 27/09/2026, divididos em grupos e com um campo de busca por frequência, cidade ou operador.

Não é preciso acertar a sintonia no ponto: o decodificador procura os tons sozinho até 500 Hz para cima ou para baixo e mostra o quanto o sinal estava fora. Numa gravação da Marinha sintonizada 500 Hz fora do canal, a leitura saiu idêntica à do canal certo.

Ao abrir a janela, o rádio entra sozinho em USB com 3 kHz de largura (os tons do ALE vão até 2.500 Hz) e, ao fechar, volta exatamente como estava. Também dá para abrir uma gravação.

Testado com uma gravação real em 14.109 kHz: as três chamadas saíram idênticas às do Sorcerer. Com chiado, a chamada inteira sai certa até cerca de 6 dB abaixo do ruído, e em 10 minutos só de chiado não apareceu nenhuma chamada falsa.

Corrigido: em computadores mais fracos, a velocidade do áudio podia variar — tocava devagar e depois pulava um pedaço. Três mudanças: na versão para Windows 7, o áudio agora é entregue ao navegador em ritmo constante, como já acontecia na versão para Windows 10/11; o tocador agora acompanha sozinho o ritmo real da placa de som (algumas placas, em PCs antigos, tocam alguns por cento mais devagar do que dizem) e corrige a diferença de forma contínua, sem saltos e com o tom certo; e, se o computador não dá conta, o rádio liga sozinho um MODO LEVE, que desenha a cachoeira e o espectro menos vezes por segundo para sobrar processador para o som. O aviso aparece na linha de informações da cachoeira, e um clique nele desliga.

Corrigido: nas janelas do HFDL e do ACARS, vários links do FlightAware abriam uma página sem o avião. O número do voo vinha com zeros à esquerda (TC0235 em vez de TC235) e o prefixo com hífen (PR-TYJ em vez de PRTYJ), formatos que o FlightAware não reconhece. Quando a mensagem traz o código ICAO do avião, o link agora vai direto por ele, o que acha o avião certo mesmo quando o HFDL corta o número do voo. Também corrigido: clicar num link enquanto chegava uma mensagem nova às vezes não fazia nada — a janela agora não se redesenha inteira e o texto fica parado enquanto o mouse ou o dedo está sobre ela.

Nova versão: **RXSDR Nativo 1.0.0** (Windows 7 SP1, 8, 10 e 11) — substitui a antiga versão "Win7". Roda inteiro no Windows, **sem navegador**, e **não precisa instalar**: é só descompactar a pasta e abrir o RXSDR.exe, como nos SDR# antigos; a configuração fica no RXSDR.ini ao lado do programa. Mesma aparência do RXSDR (S-meter de ponteiro, olho mágico, espectro dourado e cachoeira com as mesmas cores), mesmos demoduladores (AM, FM, NFM, WFM, USB, LSB, CW) e os mesmos redutores de ruído (NB, NR ESPECTRAL com a força, Redutor de Ruído), além de tom, volume, squelch e zoom. O som sai direto para a placa e acompanha sozinho o relógio dela. Aparelhos: RTL-SDR, RTL-TCP e SDRplay. Também tem memórias (os nomes aparecem em cima do espectro e um clique sintoniza), gravação do áudio em WAV, escolha da placa de som, arrastar a cachoeira e as bordas do filtro, e os botões AUTO e PADRÃO da cachoeira iguais aos da página. Menu MEMÓRIAS igual ao da página (todas, somente utilitárias, broadcast no ar agora, broadcast no ar para as Américas, broadcast todas), divisor arrastável entre espectro e cachoeira, a janela DECODERS com CW, RTTY, SITOR-B/NAVTEX, DSC, ALE 2G e o analisador de sinal desconhecido (os mesmos decodificadores do RXSDR, rodando dentro do programa) e o DMR/P25/NXDN/dPMR/YSF/D-STAR pelo dsd-fme, com o quadro dos dois slots e a voz decodificada saindo no lugar do áudio (o DMR precisa do Windows 8.1, 10 ou 11), e o TETRA (π/4-DQPSK, com o demodulador em C++ dentro do programa: sincronismo, SNR, MCC/MNC, color code, criptografia da célula, chamadas e voz quando a célula não é cifrada). A janela tem Parar, Selecionar tudo, Copiar e Salvar .txt. Também tem o IF DISPLAY (20 kHz em torno da sintonia, à esquerda da cachoeira, com largura estreita/média/larga) e as molduras e setas do painel iguais às da página. Os decodificadores HFDL, ACARS, AIS, APRS e TETRA virão em etapas.

## 1.0.64

Nova Versão RXSDR v1.0.64

Incluído o decodificador de RTTY (radioteletipo), no menu Digital Decoder.

Decodifica RTTY em código Baudot nas velocidades de 45,45, 50, 75 e 100 baud e com shift de 170, 200, 425, 450 ou 850 Hz. O tom do sinal é medido sozinho, então não é preciso acertar a sintonia no hertz; há ainda a opção de inverter mark e space para estações em LSB. As letras aparecem na tela conforme vão chegando.

Ao abrir a janela, o rádio entra sozinho em USB. Ao fechar, volta exatamente como estava: modo, largura, passo e squelch. A lista de canais traz os trechos de RTTY de radioamador (80, 40, 20, 15 e 10 m) e a previsão marítima do serviço meteorológico alemão (DWD), já com a velocidade e o shift certos de cada um.

O tom medido aparece no texto só uma vez, logo ao abrir a janela; depois disso ele atualiza apenas o campo Tom, sem picotar a mensagem. O decodificador também ficou preso à estação que está lendo: nas pausas entre chamadas, ou quando outra estação transmite ao lado (como em concurso), ele não pula mais de tom. Numa gravação real em 21.095 MHz, durante o CQ WW RTTY, as trocas de tom caíram de 12 para 2.

Em teste com sinal gerado e chiado, o texto sai correto mesmo com o sinal na altura do ruído, e em 30 minutos só de chiado nenhum caractere de lixo foi impresso.

Corrigido: na versão para Windows 7, o rádio achava o dongle e o botão ficava verde, mas não ligava — sem cachoeira, sem espectro e sem áudio, com "WS: offline" no rodapé. A versão para Windows 7 usa portas próprias (8080 e 8081), e a página procurava a ligação em tempo real no lugar errado. Agora ela encontra sozinha, tanto no Windows 7 quanto no Windows 10/11 e nos TV Box.

Corrigido: na versão para Windows 7, o RXSDR ficava preso no Gerenciador de Tarefas depois de fechar o navegador, e era preciso encerrá-lo à mão. Agora ele fecha sozinho 10 segundos depois que a última aba do rádio é fechada, igual à versão do Windows 10/11.

Removido: os arquivos de um RTTY antigo (minimodem), que não decodificava nada — apenas sorteava frases prontas — e ainda ia junto nos instaladores.

## 1.0.63

Nova Versão RXSDR v1.0.63

Incluído o NR ESPECTRAL, um redutor de ruído novo, ligado por um botão próprio ao lado do IF DISPLAY.

Ele trabalha frequência por frequência: divide o áudio em faixas estreitas e tira o chiado de cada uma separadamente, inclusive entre as notas da voz enquanto a pessoa fala.

A setinha colada ao botão abre o ajuste de FORÇA, de 0 (suave) a 100 (forte), como o Depth do SDR#. Estação fraca, enterrada no chiado, pede força baixa — força alta demais deixa a voz abafada e picotada. Chiado forte com sinal bom aguenta força alta. O padrão é 40, e o volume é compensado conforme a força, para a voz não ficar baixa. Mexer na força não faz o filtro reaprender o chiado. É a mesma família de técnica do novo redutor do SDR#, feita com métodos públicos. Rende melhor em SSB, AM e CW. Ao ligar, ele leva cerca de um segundo aprendendo o chiado da frequência.

O filtro roda no aparelho de quem está ouvindo — computador, tablet ou celular — e não no tv box ou Raspberry, então não pesa. O Slider "Redutor de Ruído" continua como estava; os dois são independentes. O rádio lembra se o NR ESPECTRAL ficou ligado.

Em telas de 1280 de largura, os controles Range, Brilho e Speed ficaram um pouco mais curtos para o botão novo caber na mesma linha.

Corrigidos os estalos rápidos que apareciam durante a fala quando o rádio era aberto pelo endereço da caixa (http://IP:8080). Nesse tipo de endereço o navegador não libera o tocador de áudio que tem folga contra atrasos, e o áudio caía num caminho antigo, montado pacote por pacote com só 20 ms de folga: qualquer atraso da rede abria um buraquinho, ouvido como um tic no meio da voz, em rajadas a cada 20 ms. Agora esse caso usa o mesmo tocador do caminho normal. Em teste com atrasos de rede simulados, os estalos no meio do áudio caíram de 21 para nenhum. Não tinha relação com o NR ESPECTRAL: os estalos existiam com ele desligado, mas o chiado os escondia.

Volume mais alto e sem distorção. O máximo do controle Volume subiu cerca de 4 dB, e a saída ganhou um limitador que segura só os picos. Antes, no volume máximo, os picos da voz chegavam a mais que o dobro do permitido e eram cortados secos pela placa de som — alto, mas áspero. Agora, com o slider em 50%, o som sai cerca de 7 dB mais alto que antes, e no máximo fica acima do máximo antigo sem nenhum pico estourado. Quem usava o volume alto vai querer baixar um pouco o slider.

## 1.0.62

Nova Versão RXSDR v1.0.62

Incluídas setas no mostrador do VFO, em cima e embaixo de cada casa de MHz para cima — 1 MHz, 10 MHz, 100 MHz e 1 GHz.

A seta de cima sobe e a de baixo desce a frequência na escala daquela casa, sem depender do STEP: a seta sobre o 1 sobe 1 MHz, a sobre o 2 de 20 MHz sobe 10 MHz, e assim por diante. Um toque dá um salto; segurando, a frequência continua andando e acelera depois de alguns saltos, para varrer a faixa depressa. Funciona com mouse, dedo e caneta.

As setas abaixo do olho mágico continuam como estavam sem alteração.

Corrigido: em telas de 1280 de largura, como a de muitos tablets, o número do VFO era mais largo que o mostrador e a primeira casa saía cortada. Agora o número diminui só o necessário para caber inteiro; em telas maiores nada mudou.

O Zoom agora amplia em volta da frequência sintonizada. Antes ele ampliava em volta do centro da cachoeira, e uma frequência longe do centro saía da tela logo no começo do zoom. Agora, ao mexer no Zoom — pelo controle ou com dois dedos no tablet —, o rádio recentra a cachoeira na frequência sintonizada, como faz o botão >.<, e ela fica no meio da tela enquanto se amplia.

## 1.0.61

Nova Versão RXSDR v1.0.61

Agora o rádio funciona com o toque dos dedos e com caneta, além do mouse.

Isso vale para a versão web — Windows e TV box — usada em tablet ou em tela sensível ao toque. Dá para sintonizar tocando na cachoeira ou no espectro, arrastar a banda com o dedo, ajustar a largura do filtro arrastando as bordas, mover o divisor entre espectro e cachoeira, e arrastar as janelas dos decodificadores. Dois dedos afastando e aproximando ampliam a cachoeira, movendo o controle Zoom junto.

Não há nada para ligar ou configurar: o mesmo rádio atende mouse, dedo e caneta ao mesmo tempo, e no computador nada mudou.

Corrigido: ao arrastar a cachoeira, o VFO andava em hertz quebrados, mostrando por exemplo 7.135.243 Hz com o STEP em 5 kHz. Agora o VFO respeita o passo escolhido, como já acontecia ao clicar. A cachoeira continua deslizando sem degraus — quem anda de passo em passo é a frequência sintonizada.

## 1.0.60

Nova Versão RXSDR v1.0.60

Incluído o IF DISPLAY, uma janela de zoom que aparece à esquerda da cachoeira.

Ela mostra 20 kHz em torno da frequência sintonizada, com espectro e cachoeira próprios, e a faixa passante do filtro marcada em amarelo — que muda de lado conforme o modo, ficando abaixo da frequência em LSB e acima em USB. Serve para ver de perto o sinal que você está escutando sem precisar mexer no zoom da cachoeira principal.

O botão IF DISPLAY fica ao lado do TETRA e pisca em laranja enquanto estiver ligado. Ao ligar, a cachoeira principal encolhe e a janela entra; ao desligar, a janela some e a cachoeira volta a ocupar toda a largura. A largura da janela tem três tamanhos, e o rádio lembra se ela ficou ligada.

Corrigido no decodificador DMR: linha sem color code válido não conta mais como quadro decodificado, e o texto do topo do painel só diz "voz" quando há voz mesmo.

Incluído no painel DMR um seletor de modo com os protocolos que o decodificador sabe procurar — DMR, P25, NXDN, dPMR, Fusion, D-STAR, M17 e outros —, além de um registro dos protocolos vistos e das mensagens decodificadas, com horário.

## 1.0.59

Nova Versão RXSDR v1.0.59

Incluído Transcrição de fala — só para versão Windows 10 e 11.

A Transcrição de fala funciona muito bem em escutas de rádio comercial WFM, em Ondas Médias.  Também funciona em SSB e Ondas Curtas, porém se o sinal for fraco e com chiados o texto sairá com erros.

Incluído botão DC no painel do radio.

No centro da cachoeira costuma ter uma linha vertical que gera apito quando sintonizado exatamente nesse ponto, ao clicar no botão DC o apito irá sumir.

Corrigido no decodificador DMR: o contador de quadros ficava parado em 0 V quando a transmissão era de voz — só os dados eram somados. Agora conta as duas coisas, e no time-slot certo.

Corrigido também o botão Invertido do DMR: ele não estava invertendo nada — o áudio saía igual nos dois. Agora inverte de verdade.


## 1.0.58

As versões 1.0.51 a 1.0.57 foram compilações de teste do suporte a hardware
com ExtIO, feitas para um amigo com um RFspace SDR-IQ. Esse suporte continua em
desenvolvimento e **não** entra nesta versão; o que vem aqui é tudo o que foi
feito em paralelo.

### SCAN — varredura de frequências

- **Janela nova de SCAN**, no menu do topo. Início, fim e passo editáveis, e o
  limiar é o **próprio squelch do rádio**, sem conversão: a medida sai da mesma
  calibração que o squelch compara.
- **A varredura não perde o começo das transmissões.** Dentro de uma janela do
  dongle, todas as frequências são vigiadas ao mesmo tempo, lendo a mesma FFT
  que desenha a cachoeira; o rádio só se move para trocar de bloco. Um scanner
  que pula de canal em canal é surdo para todos os outros enquanto está parado
  num — este não.
- **Botão de canais marítimos**, com os 89 canais de 156 a 162 MHz, lado navio
  e costeira. O canal 70 e o AIS ficam de fora: são dados e parariam a varredura
  a cada rajada.
- **Registro do que foi encontrado**, com nível, horário e quantas vezes
  apareceu. Fica gravado mesmo depois de fechar a janela, e cada linha sintoniza
  com um clique.
- **CONTINUAR** larga a frequência atual, que volta sozinha quando a portadora
  sair do ar. **IGNORAR** bane de vez, e o que foi banido some da conta.
- **Tempo máximo parado**, para sair mesmo com o sinal ainda no ar. Sem ele uma
  portadora que nunca cala prendia a varredura para sempre — e baixar os outros
  tempos não ajudava, porque a espera após o sinal só começa a contar depois que
  o nível cai.
- Passo a partir de 5 kHz em faixas largas, e o painel diz quanto tempo leva uma
  volta inteira em vez de recusar listas grandes.

### Gravar IQ bruto

- **O menu Gravar virou dois**: áudio em MP3, como antes, e **IQ bruto** — o
  sinal como sai do rádio, antes do deslocamento de frequência, da decimação e
  do demodulador. É o que permite medir desvio, alimentar um decodificador
  externo ou abrir o sinal no SDR++ depois. Gravar áudio já filtrado em 3,4 kHz
  não serve para investigar sinal nenhum.
- **Pré-gravação**: os últimos segundos ficam guardados o tempo todo, então o
  arquivo começa **antes** do clique. Quando se ouve o sinal e se aperta o
  botão, o começo da transmissão já passou.
- **Disparo por nível**, para deixar o rádio caçando sozinho a noite toda,
  encerrando após alguns segundos de silêncio.
- Arquivos fatiados a cada 2 minutos, na Área de Trabalho, com um `.json` ao
  lado guardando centro, taxa, ganho e horário. O centro anotado é o **real do
  sintonizador**, não o número do VFO — o rádio desloca o oscilador de propósito,
  e quem confiasse no VFO procuraria o sinal no lugar errado.
- Contagem de perdas na tela: um arquivo com buraco silencioso parece bom e não é.

### Correções

- **Trocar de modo só fazia efeito depois de girar o VFO.** A marca do filtro
  não era redesenhada, e a tela continuava mostrando a largura do modo anterior.
- **No TETRA, o botão Reiniciar devolvia a largura para 10 kHz.** Ele chamava
  parar e iniciar, e o parar restaura o que havia antes do painel — mas ninguém
  reaplicava os 25 kHz do TETRA depois.
- **Aviso falso de correção PPM.** A biblioteca do RTL-SDR responde "esse já é o
  valor atual" com um código que era lido como falha, e isso aparecia como erro.
- O desvio automático do oscilador virou proporção da janela, em vez de 50 kHz
  fixos. Em RTL-SDR nada muda; importa em aparelhos de janela estreita.

### Idioma

- Inglês completo nas janelas de decodificador: botões, rótulos, dicas e também
  as frases que aparecem durante a operação, que antes voltavam ao português
  assim que o decodificador atualizava a tela. O texto decodificado continua
  como veio do ar, de propósito.

### Visual

- Molduras finas nas caixas de controle, no S-meter e no VFO, e aro no botão SQL
  para deixar claro que ali existe um toque.

## 1.0.50

- **No HFDL, o prefixo da aeronave e o número do voo viram link.** Um clique
  abre a página dela no FlightAware, do mesmo jeito que já acontecia no ACARS.
  Vale para os dois rótulos que o HFDL usa — `Reg:` e `Flight:` dentro das
  mensagens ACARS, e `Flight ID:` nos relatórios de posição, que são os que
  trazem latitude e longitude.

## 1.0.49

- **Novo decodificador: HFDL, o ACARS das ondas curtas.** É por ele que as
  aeronaves conversam com as estações de terra quando estão longe demais para
  o VHF — sobre o oceano, o deserto ou o polo. Aparecem o prefixo da aeronave,
  o número do voo, a estação que atendeu e, com sorte, **a posição em latitude
  e longitude**.
- No painel você escolhe a **banda**, não a frequência. O HFDL tem várias
  frequências por banda e as estações se revezam entre elas conforme a hora e a
  propagação; ficar parado num canal só é a melhor maneira de não ouvir nada.
  São 11 bandas, de 2,9 a 22 MHz, e o rádio se ajusta sozinho ao escolher uma.
  Trocar de banda com o decodificador ligado o reinicia na banda nova.
- A janela do decodificador agora **estica junto com o texto**: antes o terminal
  tinha altura fixa, e aumentar o painel só aumentava a margem em volta.

## 1.0.48

- **Régua de informações repaginada.** Ela deixou de ser uma barra preta numa
  fileira própria e passou a flutuar sobre a parte de baixo da cachoeira, em
  negrito e com o fundo apenas escurecido — dá para ver o espectro passando
  atrás das letras, e o clique atravessa normalmente. Saiu dali a taxa de
  amostragem, que já aparecia na barra logo abaixo dizendo a mesma coisa duas
  vezes.
- **Nível do sinal em dB com uma fita de leds coloridos**, ao lado do limiar em
  que o silenciador está regulado. O número é exato mas exige ler; a fita se
  entende de relance, que é como se acompanha um sinal enquanto se varre a
  banda. Fica verde quando o silenciador está aberto. Serve para regular
  olhando, em vez de subir e descer o controle até acertar: dá para ver quanto
  o sinal sobe acima do ruído e escolher um valor no meio.
- **Mais um ajuste na sensibilidade do silenciador.** A folga sobre o ruído
  estava grande demais e barrava justamente o que se queria ouvir: um sinal
  fraco, visível na cachoeira, encostava no limiar e não abria. Agora abre.
- **Fim do apito e da linha preta no meio da cachoeira.** O dongle vaza o
  próprio oscilador bem no centro do espectro. Quando a estação ficava
  exatamente no meio — ao clicar em `>.<` ou ao arrastar a sintonia até ali —
  esse vazamento caía dentro da passagem e virava um apito junto com a
  modulação; a linha preta fina era o filtro que o remove apagando o ponto
  central. Agora o oscilador se afasta sozinho 50 kHz do VFO sempre que o
  centro cairia em cima dele. **A frequência recebida não muda**: quem
  sintoniza é o VFO por software, então o rádio continua exatamente onde você
  o colocou. Em ondas curtas nada mudou, porque ali não há misturador e
  portanto não há vazamento.

## 1.0.47

- **O silenciador (squelch) ficou bem mais sensível.** Antes, um sinal fraco —
  daqueles que você vê na cachoeira mas quase não ouve — não conseguia abrir o
  silenciador: só passava quem já chegava forte. Agora ele abre nesses sinais
  fracos também, como fazem os outros receptores SDR. Sai com um pouco de
  chiado junto, e é assim mesmo que tem de ser: melhor ouvir o sinal fraco com
  chiado do que não ouvir nada.
- **A janela do decodificador TETRA passou a guardar um registro do que
  aconteceu.** O TETRA fica horas em silêncio e de repente sincroniza, perde o
  sinal, muda de cifrado para aberto — e se você não estiver na frente do
  computador na hora, não fica sabendo. Agora tudo isso fica anotado com data e
  hora dentro da própria janela, e continua lá na próxima vez que você abrir o
  programa. Há um botão para limpar o registro quando quiser recomeçar.

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
