#!/bin/sh
# Called by 9_ffmpeg_bink.cmd through the project-local MSYS2 bash with the MSVC environment already set.
# Usage: 9_ffmpeg_bink.sh <source dir> <build dir> <install prefix> (Windows paths without spaces).
# Configures FFmpeg with the MSVC toolchain as an LGPL shared build containing only the Bink pieces:
#   libavcodec: bink (video), binkaudio_rdft + binkaudio_dct (audio) decoders
#   libavformat: bink demuxer (no protocols: the engine feeds bytes through a custom AVIOContext)
#   libavutil: what those need
# No GPL/version3/nonfree parts are enabled, autodetection of external libraries is off.
set -e
export PATH="/usr/bin:$PATH"
SRC=$(cygpath -u "$1")
BLD=$(cygpath -u "$2")
PREFIX=$(cygpath -u "$3")
mkdir -p "$BLD"
cd "$BLD"
export TMPDIR="$BLD/tmp"
mkdir -p "$TMPDIR"
echo "=== configure"
"$SRC/configure" \
  --prefix="$PREFIX" \
  --toolchain=msvc \
  --arch=x86_64 \
  --enable-shared --disable-static \
  --disable-autodetect \
  --disable-everything \
  --disable-programs --disable-doc \
  --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample \
  --disable-network \
  --disable-x86asm \
  --disable-debug \
  --enable-decoder=bink,binkaudio_rdft,binkaudio_dct \
  --enable-demuxer=bink
grep -E '^(License|CONFIG_GPL|CONFIG_NONFREE)' config.h ffbuild/config.mak 2>/dev/null || true
echo "=== make"
make -j"${NUMBER_OF_PROCESSORS:-4}"
echo "=== install"
make install
echo "=== done"
