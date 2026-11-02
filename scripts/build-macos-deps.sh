#!/bin/bash
# Builds the prebuilt static libraries the Xcode project links from a/, as
# universal (arm64 + x86_64) archives, from pinned sources:
#   OpenFst (alphacep fork) -> libfst*.a
#   Kaldi (alphacep "vosk" branch) -> kaldi-*.a
#   vosk-api -> libvosk.a
#   jsoncpp -> libjsoncpp.a, PortAudio -> libportaudio.a
# On macOS Kaldi uses the Accelerate framework for BLAS/LAPACK
# (src/makefiles/darwin.mk), so no OpenBLAS/gfortran is needed.
#
# usage: scripts/build-macos-deps.sh [output-dir]   (default: a/)
# WORK (default build/deps) holds the per-arch builds; a rerun reuses them.
# Needs: Xcode command line tools, git, cmake, autoconf, automake, libtool.
# The versions below match the headers committed in include/.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/a}"
WORK="${WORK:-$ROOT/build/deps}"
MIN="${MACOSX_DEPLOYMENT_TARGET:-11.5}"
JOBS="$(sysctl -n hw.ncpu)"

OPENFST_SHA=18e94e63870ebcf79ebb42b7035cd3cb626ec090    # alphacep/openfst master, 2024-08-23
KALDI_SHA=bc5baf14231660bd50b7d05788865b4ac6c34481      # alphacep/kaldi vosk, 2024-08-23
VOSK_SHA=eabd80a848de53e87e5943937146025d42ae570d       # alphacep/vosk-api, 2025-05-01
JSONCPP_TAG=1.9.4                                        # = include/json
PORTAUDIO_TAG=v19.7.0                                    # = include/portaudio.h

KALDI_DIRS="online2 decoder ivector gmm tree hmm lat fstext feat transform util matrix base nnet2 nnet3 chain cudamatrix lm rnnlm"
FST_LIBS="libfst libfstfar libfstfarscript libfstlookahead libfstngram libfstscript"
VOSK_SOURCES="recognizer language_model model spk_model vosk_api postprocessor"

# fetch <repo url> <commit sha or tag> <dir>
fetch() {
  # skip only a completed checkout (a failed fetch leaves an empty repo behind)
  if git -C "$3" rev-parse -q --verify HEAD >/dev/null 2>&1; then
    return 0
  fi
  mkdir -p "$3"
  git -C "$3" init -q
  local i
  for i in 1 2 3; do
    git -C "$3" fetch -q --depth 1 "$1" "$2" && break
    [ "$i" = 3 ] && return 1
    echo "fetch $1 failed, retrying"; sleep 10
  done
  git -C "$3" checkout -q FETCH_HEAD
}

build_arch() {
  local ARCH="$1" HOST
  case "$ARCH" in
    arm64) HOST=aarch64-apple-darwin ;;
    x86_64) HOST=x86_64-apple-darwin ;;
  esac
  local FLAGS="-arch $ARCH -mmacosx-version-min=$MIN"
  local D="$WORK/$ARCH"
  local F="$D/openfst" K="$D/kaldi" V="$D/vosk"
  mkdir -p "$D"

  echo "::group::OpenFst $ARCH"
  fetch https://github.com/alphacep/openfst "$OPENFST_SHA" "$F"
  if [ ! -f "$F/lib/libfst.a" ]; then
    (cd "$F" && autoreconf -i \
      && ./configure --prefix="$F" --host="$HOST" \
           --enable-static --disable-shared --enable-far --enable-ngram-fsts \
           --enable-lookahead-fsts --with-pic \
           CXX=clang++ CXXFLAGS="-O3 $FLAGS" LDFLAGS="$FLAGS" \
      && make -j"$JOBS" install)
  fi
  echo "::endgroup::"

  echo "::group::Kaldi $ARCH"
  fetch https://github.com/alphacep/kaldi "$KALDI_SHA" "$K"
  if [ ! -f "$K/src/online2/kaldi-online2.a" ]; then
    # darwin.mk hard-codes x86 -msse -msse2, which arm64 clang rejects; strip it
    # before configure, whose expf() check compiles with these flags
    (cd "$K/src" \
      && sed -i '' -e 's/ -msse -msse2//' makefiles/darwin.mk \
      && ./configure --static --static-fst --use-cuda=no --fst-root="$F" \
      && sed -i '' -e 's/ -O1 / -O3 /' kaldi.mk \
      && make -j"$JOBS" online2 lm rnnlm EXTRA_CXXFLAGS="$FLAGS" EXTRA_LDFLAGS="$FLAGS")
  fi
  echo "::endgroup::"

  echo "::group::vosk-api $ARCH"
  fetch https://github.com/alphacep/vosk-api "$VOSK_SHA" "$V"
  local s
  for s in $VOSK_SOURCES; do
    clang++ $FLAGS -O3 -std=c++17 -Wno-deprecated-declarations -fPIC -DFST_NO_DYNAMIC_LINKING \
      -I"$V/src" -I"$K/src" -I"$F/include" -c "$V/src/$s.cc" -o "$D/vosk-$s.o"
  done
  rm -f "$D/libvosk.a"
  ar rcs "$D/libvosk.a" "$D"/vosk-*.o
  echo "::endgroup::"
}

build_arch arm64
build_arch x86_64

# merge the per-arch archives
mkdir -p "$OUT"
for d in $KALDI_DIRS; do
  lipo -create "$WORK/arm64/kaldi/src/$d/kaldi-$d.a" "$WORK/x86_64/kaldi/src/$d/kaldi-$d.a" \
    -output "$OUT/kaldi-$d.a"
done
for l in $FST_LIBS; do
  lipo -create "$WORK/arm64/openfst/lib/$l.a" "$WORK/x86_64/openfst/lib/$l.a" -output "$OUT/$l.a"
done
lipo -create "$WORK/arm64/libvosk.a" "$WORK/x86_64/libvosk.a" -output "$OUT/libvosk.a"

# jsoncpp and PortAudio build universal in one CMake pass
CMAKE_UNIVERSAL=(-DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN" -DCMAKE_POLICY_VERSION_MINIMUM=3.5)

echo "::group::jsoncpp"
fetch https://github.com/open-source-parsers/jsoncpp "refs/tags/$JSONCPP_TAG" "$WORK/jsoncpp"
cmake -S "$WORK/jsoncpp" -B "$WORK/jsoncpp/out" "${CMAKE_UNIVERSAL[@]}" \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_OBJECT_LIBS=OFF \
  -DJSONCPP_WITH_TESTS=OFF -DJSONCPP_WITH_POST_BUILD_UNITTEST=OFF \
  -DJSONCPP_WITH_PKGCONFIG_SUPPORT=OFF -DJSONCPP_WITH_CMAKE_PACKAGE=OFF
cmake --build "$WORK/jsoncpp/out" -j "$JOBS"
cp "$(find "$WORK/jsoncpp/out" -name 'libjsoncpp*.a' | head -1)" "$OUT/libjsoncpp.a"
echo "::endgroup::"

echo "::group::PortAudio"
fetch https://github.com/PortAudio/portaudio "refs/tags/$PORTAUDIO_TAG" "$WORK/portaudio"
cmake -S "$WORK/portaudio" -B "$WORK/portaudio/out" "${CMAKE_UNIVERSAL[@]}" \
  -DPA_BUILD_SHARED=OFF -DPA_BUILD_STATIC=ON -DPA_BUILD_TESTS=OFF -DPA_BUILD_EXAMPLES=OFF
cmake --build "$WORK/portaudio/out" -j "$JOBS"
cp "$(find "$WORK/portaudio/out" -name 'libportaudio*.a' | head -1)" "$OUT/libportaudio.a"
echo "::endgroup::"

# every archive must hold both slices
for f in "$OUT"/*.a; do
  archs="$(lipo -archs "$f")"
  echo "$(basename "$f"): $archs"
  case "$archs" in
    *arm64*x86_64*|*x86_64*arm64*) ;;
    *) echo "::error::$f is not universal ($archs)"; exit 1 ;;
  esac
done
