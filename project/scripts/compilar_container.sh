#!/bin/bash
# ============================================================================
# RXSDR - Compilador no Container Docker do TV Box
# ============================================================================
set -e

PROJECT="/root/rxsdr_install/project"
BUILD_DIR="$PROJECT/build"
DECDIR="/opt/rxsdr/bin/decoders"
SAVE="/tmp/rxsdr_dec_save"

echo "== [CONTAINER] Realizando backup dos decoders ARM..."
rm -rf "$SAVE" && mkdir -p "$SAVE"
for b in dsd-fme tetra-rx cdecoder sdecoder ccoder scoder; do
    if [ -x "$DECDIR/$b" ]; then
        cp -a "$DECDIR/$b" "$SAVE/" && echo "Guardado: $b"
    fi
done

echo "== [CONTAINER] Configurando build com CMake..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Flags de otimizacao para Cortex-A7 (RK3229 / Allwinner H3)
ARCHFLAGS="-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -ffast-math -ftree-vectorize -funroll-loops"

cmake -DRXSDR_HEADLESS=ON \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 $ARCHFLAGS" \
      -DCMAKE_INSTALL_PREFIX=/opt/rxsdr ..

echo "== [CONTAINER] Compilando..."
cmake --build . --config Release -j2

echo "== [CONTAINER] Instalando em /opt/rxsdr..."
cmake --install .

echo "== [CONTAINER] Restaurando decoders ARM..."
if [ -d "$DECDIR" ]; then
    for b in dsd-fme tetra-rx cdecoder sdecoder ccoder scoder; do
        if [ -f "$SAVE/$b" ]; then
            cp -a "$SAVE/$b" "$DECDIR/$b"
            chmod +x "$DECDIR/$b"
            echo "Restaurado: $b"
        fi
    done
fi

echo "== [CONTAINER] Ajustando permissoes dos executaveis e scripts..."
if [ -d "$DECDIR" ]; then
    for f in tetra tetra_decoder.py tetra_demod.py dsd-fme tetra-rx cdecoder sdecoder ccoder scoder \
             selcal_runner.py cw_runner.py dsc_runner.py sitorb_runner.py sondedump_runner.py \
             bpsk_runner.py minimodem_runner.py pactor_runner.py adsb_runner.py; do
        [ -e "$DECDIR/$f" ] && chmod +x "$DECDIR/$f" 2>/dev/null && echo "  +x $f"
    done
fi

echo "== [CONTAINER] Tudo pronto e compilado!"
