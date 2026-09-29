#include "Radio.h"
#include "Config.h"

#include "../dsp/DemodAM.h"
#include "../dsp/DemodCW.h"
#include "../dsp/DemodFM.h"
#include "../dsp/DemodSSB.h"
#include "../dsp/FftProcessor.h"
#include "../sdr/DeviceFactory.h"
#include "../sdr/ISdrDevice.h"
#include "../sdr/RtlSdrDevice.h"
#include "../sdr/SdrplayDevice.h"
#include "../util/Logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace masdr {

static constexpr double kPi = 3.14159265358979323846;

static int64_t agoraMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

Radio::Radio() = default;
Radio::~Radio() { encerrar(); }

bool Radio::iniciar()
{
    auto& c = Config::instance();
    vfo_.store((uint64_t)c.num("vfo", 7100000));
    {
        std::lock_guard<std::mutex> lk(modoMutex_);
        modo_ = c.str("modo", "LSB");
    }
    bw_.store((int)c.num("bw", 2400));
    fftN_ = (size_t)std::max(1024, c.fftSize());
    fft_ = std::make_unique<FftProcessor>(fftN_);
    novoDemod();
    if (!som_.iniciar(indiceSaida(c.str("audio_saida"))))
        Logger::error("Som: nao abriu a placa de som (" + som_.erro() + ")");
    return true;
}

int Radio::indiceSaida(const std::string& nome)
{
    if (nome.empty()) return -1;
    const auto v = SomNativo::listarSaidas();
    for (size_t i = 0; i < v.size(); ++i) if (v[i] == nome) return (int)i;
    return -1;                       // placa sumiu (USB desligada): usa a padrao
}

bool Radio::trocarSaida(const std::string& nome)
{
    Config::instance().set("audio_saida", nome);
    const AjusteAudio a = som_.ajuste();
    som_.parar();
    const bool ok = som_.iniciar(indiceSaida(nome));
    som_.ajustar(a);
    return ok;
}

void Radio::encerrar()
{
    som_.pararGravacao();
    std::string e;
    if (ligado_) ligar(false, e);
    {
        std::lock_guard<std::mutex> lk(devMutex_);
        if (dev_) { dev_->stop(); dev_->close(); dev_.reset(); }
    }
    som_.parar();
}

std::vector<DispositivoInfo> Radio::listar(const std::string& tipoAberto, const std::string& serialAberto, bool sdrplay)
{
    std::vector<DispositivoInfo> v;
    // Com um RTL-SDR aberto, NAO se varre o USB (o RXSDR principal faz igual):
    // perguntar ao libusb por um aparelho que esta no meio de uma leitura
    // arrisca derrubar o programa. O que esta aberto entra como "em uso".
    if (tipoAberto == "rtlsdr" && !serialAberto.empty())
        v.push_back({"rtlsdr", serialAberto, "RTL-SDR (em uso)"});
    else
        for (const auto& i : RtlSdrDevice::enumerate())  v.push_back({"rtlsdr", i.serial, i.name});
    if (sdrplay)
        for (const auto& i : SdrplayDevice::enumerate()) v.push_back({"sdrplay", i.serial, i.name});
    return v;
}

// ---------------------------------------------------------------------------
//  Abre o aparecelho escolhido e ja aplica a configuracao. Nao liga.
// ---------------------------------------------------------------------------
bool Radio::selecionar(const std::string& tipo, const std::string& serialPedido, std::string& erro)
{
    std::lock_guard<std::mutex> lk(devMutex_);
    auto& c = Config::instance();
    if (dev_) {
        dev_->stop(); dev_->close(); dev_.reset();
        ligado_ = false;
    }
    auto d = DeviceFactory::create(tipo);
    if (!d) { erro = "Tipo de aparelho desconhecido: " + tipo; return false; }

    std::string arg = serialPedido;
    if (tipo == "rtltcp") {
        if (arg.find(':') == std::string::npos)
            arg = c.tcpHost() + ":" + std::to_string(c.tcpPort());
    }
    const uint64_t f = vfo_.load();
    qAtual_ = (tipo != "sdrplay") && c.quadratureEm(f);
    d->setSampleRate(c.sampleRate());
    d->setQuadrature(qAtual_);
    d->setPpm(c.ppm());
    d->setBias(c.biasT());
    d->setCenterFreq(f);
    if (tipo != "sdrplay") d->setGain(c.agc() ? -1 : c.gainTenths());

    if (!d->open(arg)) {
        erro = d->lastError();
        if (erro.empty()) erro = "Nao foi possivel abrir o " + tipo + (arg.empty() ? "" : " (" + arg + ")");
        return false;
    }
    dev_ = d;
    tipo_ = tipo;
    serial_ = d->serial().empty() ? arg : d->serial();
    c.set("device", tipo_);
    c.set("serial", tipo == "rtltcp" ? std::string() : serial_);
    dcBlock_.reset();
    fase_ = 0.0;
    // Algumas opcoes so pegam depois do open (mesma ordem do RXSDR principal).
    dev_->setSampleRate(c.sampleRate());
    dev_->setQuadrature(qAtual_);
    dev_->setPpm(c.ppm());
    ppmAplicado_ = c.ppm();
    dev_->setBias(c.biasT());
    dev_->setCenterFreq(f);
    if (tipo_ == "sdrplay") {
        if (auto* s = dynamic_cast<SdrplayDevice*>(dev_.get()))
            s->setSdrplayParams(c.sdrplayIfMode(), c.sdrplayLna(), c.sdrplayIfGain(), c.sdrplayIfAgc(), c.sdrplayBw());
    } else {
        dev_->setGain(c.agc() ? -1 : c.gainTenths());
    }
    ligarCallback();
    Logger::info("Aparelho: " + tipo_ + " (" + serial_ + ")");
    return true;
}

bool Radio::ligar(bool on, std::string& erro)
{
    if (on && !dev_) {
        auto& c = Config::instance();
        if (!selecionar(c.device(), c.serial(), erro)) return false;
    }
    std::lock_guard<std::mutex> lk(devMutex_);
    if (!dev_) return !on;
    if (on) {
        if (!ligado_) { dcBlock_.reset(); dev_->start(); }
        ligado_ = true;
    } else {
        if (ligado_) dev_->stop();
        ligado_ = false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Sintonia
// ---------------------------------------------------------------------------
bool Radio::sintonizar(uint64_t hz)
{
    vfo_.store(hz);
    Config::instance().set("vfo", (long long)hz);
    std::lock_guard<std::mutex> lk(devMutex_);
    if (!dev_) return false;
    const auto& c = Config::instance();
    const bool q = (tipo_ != "sdrplay") && c.quadratureEm(hz);
    const uint32_t sr = dev_->sampleRate();
    const int64_t diff = std::llabs((int64_t)hz - (int64_t)dev_->centerFreq());
    // Fora da janela (ou mudou HF <-> VHF no modo Q automatico): recentraliza.
    if (q != qAtual_ || diff > (int64_t)(sr * 0.45)) {
        if (q != qAtual_) { dev_->setQuadrature(q); qAtual_ = q; }
        dev_->setCenterFreq(hz);
        if (tipo_ == "sdrplay") {
            if (auto* s = dynamic_cast<SdrplayDevice*>(dev_.get()))
                s->setSdrplayParams(c.sdrplayIfMode(), c.sdrplayLna(), c.sdrplayIfGain(), c.sdrplayIfAgc(), c.sdrplayBw());
        } else {
            dev_->setGain(c.agc() ? -1 : c.gainTenths());
        }
        return true;
    }
    return false;
}

void Radio::centralizar(uint64_t hz)
{
    std::lock_guard<std::mutex> lk(devMutex_);
    if (!dev_) return;
    const auto& c = Config::instance();
    const bool q = (tipo_ != "sdrplay") && c.quadratureEm(hz);
    if (q != qAtual_) { dev_->setQuadrature(q); qAtual_ = q; }
    dev_->setCenterFreq(hz);
    if (tipo_ != "sdrplay") dev_->setGain(c.agc() ? -1 : c.gainTenths());
}

uint64_t Radio::centro() const
{
    std::lock_guard<std::mutex> lk(devMutex_);
    auto d = dev_;
    return d ? d->centerFreq() : vfo_.load();
}

uint32_t Radio::taxa() const
{
    std::lock_guard<std::mutex> lk(devMutex_);
    auto d = dev_;
    return d ? d->sampleRate() : Config::instance().sampleRate();
}

bool Radio::qAtivo() const { return qAtual_; }

void Radio::setModo(const std::string& m)
{
    {
        std::lock_guard<std::mutex> lk(modoMutex_);
        if (m == modo_) return;
        modo_ = m;
    }
    Config::instance().set("modo", m);
    novoDemod();
}

std::string Radio::modo() const
{
    std::lock_guard<std::mutex> lk(modoMutex_);
    return modo_;
}

void Radio::setBanda(int hz)
{
    bw_.store(hz);
    Config::instance().set("bw", hz);
    std::lock_guard<std::mutex> lk(demodMutex_);
    if (demod_) demod_->setBandwidth((uint32_t)hz);
}

void Radio::setGanho(int tenths)
{
    auto& c = Config::instance();
    c.set("gain", tenths);
    std::lock_guard<std::mutex> lk(devMutex_);
    if (dev_ && tipo_ != "sdrplay") dev_->setGain(c.agc() ? -1 : tenths);
}

void Radio::aplicarConfig()
{
    auto& c = Config::instance();
    {
        std::lock_guard<std::mutex> lk(fftMutex_);
        const size_t n = (size_t)std::max(1024, c.fftSize());
        if (!fft_ || n != fftN_) { fft_ = std::make_unique<FftProcessor>(n); fftN_ = n; }
    }
    std::lock_guard<std::mutex> lk(devMutex_);
    if (!dev_) return;
    const uint64_t f = vfo_.load();
    const bool q = (tipo_ != "sdrplay") && c.quadratureEm(f);
    // TUDO muda com o dongle andando - nunca se para e religa a leitura.
    // Cancelar o read_async e reabrir em seguida derrubava o programa dentro
    // do libusb (aconteceu ao aplicar AGC e ao aplicar PPM). O rtl_tcp faz
    // igual: taxa, PPM, ganho e amostragem direta sao trocados ao vivo.
    const bool reinicia = dev_->sampleRate() != c.sampleRate() || q != qAtual_ || c.ppm() != ppmAplicado_;
    qAtual_ = q;
    if (reinicia) {
        dev_->setSampleRate(c.sampleRate());
        dev_->setQuadrature(qAtual_);
        dev_->setPpm(c.ppm());
        ppmAplicado_ = c.ppm();
        dev_->setCenterFreq(f);
    }
    dev_->setBias(c.biasT());
    if (tipo_ == "sdrplay") {
        if (auto* s = dynamic_cast<SdrplayDevice*>(dev_.get()))
            s->setSdrplayParams(c.sdrplayIfMode(), c.sdrplayLna(), c.sdrplayIfGain(), c.sdrplayIfAgc(), c.sdrplayBw());
    } else {
        dev_->setGain(c.agc() ? -1 : c.gainTenths());
    }
    if (reinicia) dcBlock_.reset();
}

// ---------------------------------------------------------------------------
//  Demodulador
// ---------------------------------------------------------------------------
void Radio::novoDemod()
{
    const std::string m = modo();
    std::unique_ptr<Demodulator> d;
    if (m == "AM") d = std::make_unique<DemodAM>();
    else if (m == "FM" || m == "NFM" || m == "WFM") { d = std::make_unique<DemodFM>(); d->setMode(m); }
    else if (m == "CW") d = std::make_unique<DemodCW>();
    else { d = std::make_unique<DemodSSB>(); d->setMode(m); }
    d->setBandwidth((uint32_t)bw_.load());
    d->setAudioCallback([this](const std::vector<int16_t>& pcm, uint32_t sps) { aoAudio(pcm, sps); });
    std::lock_guard<std::mutex> lk(demodMutex_);
    demod_ = std::move(d);
}

void Radio::aoAudio(const std::vector<int16_t>& pcm, uint32_t sps)
{
    if (pcm.empty() || sps == 0) return;
    dec_.empurrar(pcm.data(), pcm.size(), sps);     // antes do volume e do NR
    if (dec_.substituiAudio()) {
        // DMR/TETRA: no lugar do chiado do FM toca a voz decodificada.
        // O bloco tem o mesmo tamanho, entao o relogio do som segue o do dongle.
        std::vector<int16_t> voz(pcm.size());
        dec_.puxarVoz(voz.data(), voz.size(), sps);
        som_.empurrar(voz.data(), voz.size(), sps);
        return;
    }
    som_.empurrar(pcm.data(), pcm.size(), sps);
}

// ---------------------------------------------------------------------------
//  Quadros de FFT para a tela
// ---------------------------------------------------------------------------
bool Radio::pegarFft(QuadroFft& out)
{
    std::lock_guard<std::mutex> lk(quadroMutex_);
    if (ultimo_.seq == seqLido_ || ultimo_.bins.empty()) return false;
    out = ultimo_;
    seqLido_ = ultimo_.seq;
    return true;
}

bool Radio::aoVivo() const
{
    return ligado_ && (agoraMs() - ultimoIqMs_.load()) < 2000;
}

// ---------------------------------------------------------------------------
//  O caminho do IQ - traducao do wireDeviceCallback do RXSDR principal
//  (os comentarios longos de la explicam o porque de cada ganho).
// ---------------------------------------------------------------------------
void Radio::ligarCallback()
{
    if (!dev_) return;
    std::weak_ptr<ISdrDevice> fraco = dev_;
    dev_->setCallback([this, fraco](const std::complex<float>* iq, size_t n) {
        auto dev = fraco.lock();
        if (!dev || !iq || n == 0) return;
        ultimoIqMs_.store(agoraMs());

        const uint64_t vfoHz = vfo_.load(std::memory_order_relaxed);
        const auto& cfg = Config::instance();
        const int gainTenths = cfg.gainTenths();
        const float relDb = std::clamp((float(gainTenths) - 280.0f) / 10.0f, -24.0f, 36.0f);
        float uiGain = std::pow(10.0f, relDb / 20.0f);
        const bool direta = cfg.quadratureEm(vfoHz);
        const bool familiaRtl = (tipo_ == "rtlsdr" || tipo_ == "rtltcp");
        if (cfg.agc() || tipo_ == "sdrplay") uiGain = 1.0f;
        if (cfg.agc() && familiaRtl) uiGain *= 1.380f;       // +2,8 dB
        if (direta && familiaRtl)    uiGain *= 7.943f;       // +18 dB em HF
        if (!direta && familiaRtl)   uiGain *= 1.259f;       // +2 dB em VHF

        std::vector<std::complex<float>> work(iq, iq + n);
        const bool usaDc = (tipo_ != "sdrplay");
        const int64_t desvio = (int64_t)vfoHz - (int64_t)dev->centerFreq();
        const bool vfoNoDc = usaDc && !cfg.dcRemove()
                          && std::llabs(desvio) < (int64_t)(dev->sampleRate() / 500.0);
        std::vector<std::complex<float>> semDc;
        if (vfoNoDc) semDc.assign(iq, iq + n);
        if (usaDc) for (size_t i = 0; i < n; ++i) work[i] = dcBlock_.process(work[i]);
        if (uiGain != 1.0f) {
            for (auto& s : work)  s *= uiGain;
            for (auto& s : semDc) s *= uiGain;
        }
        const std::complex<float>* src = work.data();
        const std::complex<float>* srcDemod = vfoNoDc ? semDc.data() : work.data();

        float maxP = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            const float p = src[i].real() * src[i].real() + src[i].imag() * src[i].imag();
            if (p > maxP) maxP = p;
        }
        pico_.store(10.f * std::log10(maxP + 1e-12f));

        // FFT: acumula tudo; entrega a media a cada 40 ms (25 quadros/s)
        {
            std::lock_guard<std::mutex> lk(fftMutex_);
            if (fft_) fft_->accumulate(src, n);
            const int64_t t = agoraMs();
            if (t - ultimoFftMs_ >= 40 && fft_) {
                ultimoFftMs_ = t;
                std::vector<int8_t> bins = fft_->hasAccumulated() ? fft_->takeAverageDbfs()
                                                                  : fft_->computeDbfs(src, n);
                if (!bins.empty()) {
                    std::lock_guard<std::mutex> lq(quadroMutex_);
                    ultimo_.bins.swap(bins);
                    ultimo_.centro = dev->centerFreq();
                    ultimo_.taxa = dev->sampleRate();
                    ++ultimo_.seq;
                }
            }
        }

        dec_.empurrarIQCru(src, n, dev->sampleRate(), dev->centerFreq());   // HFDL: a banda inteira

        // NCO: poe o VFO no zero para o demodulador
        std::vector<std::complex<float>> girado;
        const std::complex<float>* dsrc = srcDemod;
        if (desvio != 0) {
            girado.resize(n);
            const double passo = -2.0 * kPi * double(desvio) / double(dev->sampleRate());
            const std::complex<float> step((float)std::cos(passo), (float)std::sin(passo));
            std::complex<float> nco((float)std::cos(fase_), (float)std::sin(fase_));
            for (size_t i = 0; i < n; ++i) {
                girado[i] = srcDemod[i] * nco;
                nco *= step;
                if ((i & 1023) == 0) {
                    const float l2 = nco.real() * nco.real() + nco.imag() * nco.imag();
                    if (std::abs(l2 - 1.0f) > 1e-4f) nco /= std::sqrt(l2);
                }
            }
            fase_ += passo * double(n);
            fase_ = std::fmod(fase_ + kPi, 2.0 * kPi);
            if (fase_ < 0.0) fase_ += 2.0 * kPi;
            fase_ -= kPi;
            dsrc = girado.data();
        }

        dec_.empurrarIQ(dsrc, n, dev->sampleRate());   // TETRA: IQ com o VFO no zero
        std::lock_guard<std::mutex> lk(demodMutex_);
        if (demod_) {
            // DMR: discriminador cru, sem de-enfase nem filtros de audio (igual ao RXSDR)
            demod_->setIsDigital(dec_.tipo() == Decoders::DMR);
            demod_->process(dsrc, n, dev->sampleRate());
        }
    });
}

} // namespace masdr
