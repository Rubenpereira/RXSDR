#pragma once
// ---------------------------------------------------------------------------
//  Idioma - portugues / ingles do RXSDR Nativo
//
//  O dicionario e indexado pelo PROPRIO TEXTO EM PORTUGUES (como na versao de
//  navegador): T("Volume (MUDO)") devolve "Volume (MUTED)" em ingles e o
//  proprio texto em portugues. Frase que nao esta no dicionario sai como
//  esta - nada quebra se faltar uma.
//
//  NUNCA passar por aqui o que foi RECEBIDO (texto do NAVTEX, do PACTOR,
//  nomes das memorias, mensagens dos decodificadores externos): traduzir isso
//  seria adulterar o dado. So os textos da tela e os avisos do proprio RXSDR.
//
//  A escolha fica no RXSDR.ini (lang=pt ou lang=en). Sem a chave, o programa
//  pergunta na primeira abertura.
// ---------------------------------------------------------------------------
#include <string>

namespace masdr {

void definirIngles(bool en);
bool emIngles();

// textos fixos da tela (frase inteira)
const char* T(const char* pt);
std::string T(const std::string& pt);

// nomes das listas (estacoes, bandas, emissoras): troca so os pedacos em
// portugues e deixa os nomes proprios. TL guarda o resultado (so para textos
// fixos do programa); TLs nao guarda (para textos montados na hora).
const char* TL(const char* pt);
std::string TLs(const std::string& pt);

// avisos que os nucleos dos decodificadores escrevem entre colchetes
std::string TAvisos(const std::string& s);
// linha de mensagem do DSC (montada pelo nucleo)
std::string TDsc(const std::string& s);
// linha do relatorio da ANALISE
std::string TAnalise(const std::string& s);

} // namespace masdr
