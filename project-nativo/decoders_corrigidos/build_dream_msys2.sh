#!/bin/bash
# Compila o Dream (DRM) em modo console, com stdin/stdout, para o RXSDR.
export MSYSTEM=UCRT64
source /etc/profile
set -e
cd /home/src
LOG=/home/src/build_drm.log
exec > "$LOG" 2>&1
cd dream-drm
if [ ! -f .rxsdr_patch_ok ]; then
  base64 -d ../rxsdr_dream.patch.gz.b64 | gunzip > ../rxsdr_dream.patch
  git apply --whitespace=nowarn ../rxsdr_dream.patch
  touch .rxsdr_patch_ok
  echo "patch aplicado"
fi
qmake CONFIG+=console CONFIG+=fdk-aac CONFIG+=stdio CONFIG+=speexdsp CONFIG+=release "QMAKE_CXXFLAGS+=-std=gnu++14" dream.pro
mingw32-make -j8
ls -la release/dream.exe dream.exe 2>/dev/null || true
echo FIM_OK
