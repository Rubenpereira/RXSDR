#pragma once
#include <QString>

namespace masdr {

// ---------------------------------------------------------------------------
//  Onde fica a Area de Trabalho DE VERDADE
//
//  O QStandardPaths::DesktopLocation devolve %USERPROFILE%\Desktop, e ha anos
//  isso deixou de ser sempre verdade no Windows. Com o OneDrive ligado - e ele
//  vem ligado de fabrica em muita maquina -, a Area de Trabalho MUDA DE LUGAR
//  para %USERPROFILE%\OneDrive\Area de Trabalho, e o Windows anota isso no
//  registro das pastas conhecidas.
//
//  O detalhe cruel: a pasta antiga continua existindo, vazia e escondida do
//  usuario. Entao gravar la da CERTO - o arquivo e criado, o programa informa
//  sucesso - e o dono nunca ve nada. Foi o que aconteceu com a gravacao de
//  audio e com a de IQ ao mesmo tempo.
//
//  A funcao abaixo pergunta ao proprio Windows qual e a pasta, o que respeita
//  o desvio do OneDrive e qualquer outro que o usuario tenha feito.
// ---------------------------------------------------------------------------
QString areaDeTrabalho();

} // namespace masdr
