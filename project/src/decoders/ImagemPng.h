#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QString>
#include <QUrl>
#include <cstdint>

#include "../util/Caminhos.h"

namespace masdr {

// ---------------------------------------------------------------------------
//  Utilidades das imagens do SSTV e do WEFAX: PNG em memoria (para a pagina e
//  para o arquivo) e a pasta na Area de Trabalho - a mesma onde ja caem as
//  gravacoes de audio e de IQ. Ao lado do RXSDR.exe nao da: instalado, ele
//  fica em Arquivos de Programas, onde o programa nao pode escrever.
// ---------------------------------------------------------------------------

// ARGB (0xAARRGGBB) -> PNG
inline QByteArray pngArgb(const uint32_t* px, int w, int h)
{
    if (!px || w <= 0 || h <= 0) return QByteArray();
    const QImage img(reinterpret_cast<const uchar*>(px), w, h, w * 4, QImage::Format_RGB32);
    QByteArray out;
    QBuffer b(&out);
    b.open(QIODevice::WriteOnly);
    img.save(&b, "PNG");
    return out;
}

// tons de cinza (1 byte por pixel) -> PNG
inline QByteArray pngCinza(const uint8_t* px, int w, int h)
{
    if (!px || w <= 0 || h <= 0) return QByteArray();
    const QImage img(reinterpret_cast<const uchar*>(px), w, h, w, QImage::Format_Grayscale8);
    QByteArray out;
    QBuffer b(&out);
    b.open(QIODevice::WriteOnly);
    img.save(&b, "PNG");
    return out;
}

inline QString pastaImagens(const QString& sub)
{
    return QDir(areaDeTrabalho()).filePath(sub);
}

// Grava e devolve o caminho (vazio se falhou)
inline QString gravarArquivo(const QString& pasta, const QString& nome, const QByteArray& dados)
{
    QDir().mkpath(pasta);
    const QString cam = QDir(pasta).filePath(nome);
    QFile f(cam);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    const bool ok = f.write(dados) == dados.size();
    f.close();
    return ok ? QDir::toNativeSeparators(cam) : QString();
}

// Abre a pasta no gerenciador de arquivos (na thread principal)
inline void abrirPasta(const QString& pasta)
{
    QDir().mkpath(pasta);
    QCoreApplication* app = QCoreApplication::instance();
    if (!app) return;
    QMetaObject::invokeMethod(app, [pasta]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(pasta));
    }, Qt::QueuedConnection);
}

} // namespace masdr
