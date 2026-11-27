#!/usr/bin/env bash
# RD4tvOS Probe 빌드 — 라이브러리부터 IPA 까지
#
# 맥에 Xcode 가 없으므로 전부 CI 에서 돈다. 산출물 IPA 는 GitHub 릴리즈로 올려야
# 애플TV 에 설치된다(릴리즈하지 않은 빌드물은 설치 불가).
#
# 레시피는 FreeRDP tvOS 스파이크에서 확정된 것을 그대로 쓴다.
# 세부 근거는 docs/SPIKE_FREERDP_TVOS.md 와 프로젝트 문서 §7.2 를 본다.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
WORK="${WORK:-$PWD/app-work}"
OUT="${OUT:-$PWD/app-out}"
DEPLOY_TARGET="${DEPLOY_TARGET:-16.0}"
FREERDP_REF="${FREERDP_REF:-auto}"
PREFIX="$WORK/prefix"

mkdir -p "$WORK" "$OUT"
say() { printf '\n=== %s ===\n' "$*"; }

# ---------------------------------------------------------------- 1. 툴체인
say "1. ios-cmake 툴체인"
[ -d "$WORK/ios-cmake" ] || git clone --depth 1 https://github.com/leetal/ios-cmake "$WORK/ios-cmake" >/dev/null 2>&1
TOOLCHAIN="$WORK/ios-cmake/ios.toolchain.cmake"
[ -f "$TOOLCHAIN" ] || { echo "툴체인을 받지 못했다"; exit 1; }

# ------------------------------------------------- 2. OpenSSL (tvOS arm64)
say "2. OpenSSL tvOS arm64"
if [ ! -f "$WORK/openssl-tvos/lib/libcrypto.a" ]; then
  [ -d "$WORK/openssl-apple" ] || git clone --depth 1 https://github.com/passepartoutvpn/openssl-apple "$WORK/openssl-apple" >/dev/null 2>&1
  # 기기 타깃은 tvos-cross-arm64 다. tvos-sim-* 은 시뮬레이터이므로 배제한다.
  # --deprecated 가 필요하다 — 기본값 no-deprecated 로 빌드하면 OpenSSL 3.x 에서
  # SSL_get_peer_certificate 호환 매크로가 사라져 libfreerdp/crypto/tls.c 가 깨진다.
  ( cd "$WORK/openssl-apple" && ./build-libssl.sh --targets="tvos-cross-arm64" \
      --deprecated --min-tvos-sdk="$DEPLOY_TARGET" ) > "$OUT/openssl-build.log" 2>&1
  mkdir -p "$WORK/openssl-tvos/lib" "$WORK/openssl-tvos/include"
  lib="$(find "$WORK/openssl-apple" -name 'libcrypto*.a' | head -1)"
  [ -n "$lib" ] || { echo "libcrypto 를 찾지 못했다 — openssl-build.log 를 보라"; exit 1; }
  cp "$lib" "$WORK/openssl-tvos/lib/libcrypto.a"
  cp "$(dirname "$lib")"/libssl*.a "$WORK/openssl-tvos/lib/libssl.a"
  inc="$(find "$WORK/openssl-apple" -type d -name openssl -path '*include*' | head -1)"
  cp -R "$(dirname "$inc")/." "$WORK/openssl-tvos/include/"
fi
OPENSSL_ROOT="$WORK/openssl-tvos"
# 플랫폼을 문서가 아니라 로드 커맨드로 확인한다
for f in "$OPENSSL_ROOT"/lib/*.a; do
  p="$(otool -lv "$f" 2>/dev/null | awk '/LC_BUILD_VERSION/{f=1} f&&/platform/{print $2; exit}')"
  echo "  $(basename "$f") platform=$p arch=$(lipo -archs "$f")"
  [ "$p" = "TVOS" ] || { echo "OpenSSL 이 tvOS 가 아니다 ($p) — 중단"; exit 1; }
done

# ----------------------------------------------------- 3. winpr (+freerdp)
say "3. FreeRDP/winpr tvOS 빌드·설치"
if [ ! -f "$PREFIX/lib/libwinpr3.a" ]; then
  if [ "$FREERDP_REF" = "auto" ]; then
    FREERDP_REF="$(git ls-remote --tags --refs https://github.com/FreeRDP/FreeRDP \
      | awk -F/ '{print $NF}' | grep -E '^3\.[0-9]+\.[0-9]+$' \
      | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)"
  fi
  echo "  FreeRDP ref = $FREERDP_REF"
  [ -d "$WORK/FreeRDP" ] || git clone --depth 1 --branch "$FREERDP_REF" \
      https://github.com/FreeRDP/FreeRDP "$WORK/FreeRDP" >/dev/null 2>&1

  # VideoToolbox 조건에 tvOS 분기를 더한다(1b 의 H.264 경로를 위해 미리).
  python3 "$ROOT/../spike/freerdp-tvos/patch_videotoolbox.py" \
      "$WORK/FreeRDP/cmake/ConfigOptionsVideoToolbox.cmake" || true

  cmake -S "$WORK/FreeRDP" -B "$WORK/build-libs" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DPLATFORM=TVOS \
    -DDEPLOYMENT_TARGET="$DEPLOY_TARGET" -DENABLE_BITCODE=OFF -DENABLE_ARC=ON \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF \
    -DWITH_CLIENT=OFF -DWITH_CLIENT_COMMON=OFF -DWITH_CLIENT_SDL=OFF \
    -DWITH_SERVER=OFF -DWITH_SAMPLE=OFF -DWITH_PROXY=OFF -DWITH_SHADOW=OFF \
    -DBUILTIN_CHANNELS=ON -DWITH_WINPR_TOOLS=OFF -DWITH_MANPAGES=OFF \
    -DWITH_KRB5=OFF -DWITH_PKCS11=OFF -DWITH_PCSC=OFF \
    -DWITH_X11=OFF -DWITH_WAYLAND=OFF -DWITH_CUPS=OFF \
    -DWITH_ALSA=OFF -DWITH_PULSE=OFF -DWITH_OSS=OFF \
    -DWITH_FFMPEG=OFF -DWITH_SWSCALE=OFF -DWITH_CAIRO=OFF \
    -DWITH_OPENH264=OFF -DWITH_LIBSYSTEMD=OFF -DWITH_SOXR=OFF \
    -DWITH_JSON_DISABLED=ON -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
    -DWITH_OPENSSL=ON -DOPENSSL_USE_STATIC_LIBS=ON \
    -DOPENSSL_ROOT_DIR="$OPENSSL_ROOT" -DOPENSSL_INCLUDE_DIR="$OPENSSL_ROOT/include" \
    -DWITH_INTERNAL_RC4=ON -DWITH_INTERNAL_MD4=ON \
    -DCMAKE_C_FLAGS="-include $ROOT/../spike/freerdp-tvos/tvos_shim.h" \
    > "$OUT/libs-configure.log" 2>&1
  cmake --build "$WORK/build-libs" --config Release -- -j3 > "$OUT/libs-build.log" 2>&1
  cmake --install "$WORK/build-libs" --config Release > "$OUT/libs-install.log" 2>&1 || true
fi

# 설치 경로 구조를 추측하지 않고 찾아낸다
WINPR_LIB="$(find "$PREFIX" "$WORK/build-libs" -name 'libwinpr3.a' 2>/dev/null | head -1)"
WINPR_HDR="$(find "$PREFIX" "$WORK/FreeRDP/winpr/include" -path '*winpr/sspi.h' 2>/dev/null | head -1)"
[ -n "$WINPR_LIB" ] || { echo "libwinpr3.a 를 찾지 못했다"; tail -30 "$OUT/libs-build.log"; exit 1; }
[ -n "$WINPR_HDR" ] || { echo "winpr/sspi.h 를 찾지 못했다"; exit 1; }
WINPR_INC="$(dirname "$(dirname "$WINPR_HDR")")"
echo "  lib = $WINPR_LIB"
echo "  inc = $WINPR_INC"
p="$(otool -lv "$WINPR_LIB" 2>/dev/null | awk '/LC_BUILD_VERSION/{f=1} f&&/platform/{print $2; exit}')"
echo "  platform = $p"
[ "$p" = "TVOS" ] || { echo "winpr 가 tvOS 가 아니다 ($p) — 중단"; exit 1; }

# winpr 가 생성한 설정 헤더(winpr/config.h 등)도 필요하다
GEN_INC=""
for d in $(find "$WORK/build-libs" -path '*winpr/include' -type d 2>/dev/null | head -3); do
  GEN_INC="$GEN_INC;$d"
done

# ------------------------------------------------------------- 4. 앱 빌드
say "4. 앱 빌드"
cmake -S "$ROOT" -B "$WORK/build-app" -G Xcode \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DPLATFORM=TVOS \
  -DDEPLOYMENT_TARGET="$DEPLOY_TARGET" -DENABLE_BITCODE=OFF -DENABLE_ARC=ON \
  -DWINPR_INCLUDE_DIR="$WINPR_INC${GEN_INC}" \
  -DWINPR_LIBRARY="$WINPR_LIB" \
  -DOPENSSL_ROOT="$OPENSSL_ROOT" \
  -DAPP_BUILD="${GITHUB_RUN_NUMBER:-1}" \
  > "$OUT/app-configure.log" 2>&1
cmake --build "$WORK/build-app" --config Release > "$OUT/app-build.log" 2>&1

APP="$(find "$WORK/build-app" -maxdepth 3 -name 'rd4tvos-probe.app' -type d | head -1)"
[ -n "$APP" ] || { echo "앱 번들을 찾지 못했다"; tail -40 "$OUT/app-build.log"; exit 1; }
echo "  app = $APP"
otool -lv "$APP/rd4tvos-probe" 2>/dev/null | awk '/LC_BUILD_VERSION/{f=1} f&&/(platform|minos)/{print "  "$1" "$2}' | head -2

# ------------------------------------------------------------ 5. IPA 포장
say "5. IPA 포장"
rm -rf "$WORK/Payload" "$OUT/rd4tvos-probe.ipa"
mkdir -p "$WORK/Payload"
cp -R "$APP" "$WORK/Payload/"
( cd "$WORK" && zip -qry "$OUT/rd4tvos-probe.ipa" Payload )
ls -la "$OUT/rd4tvos-probe.ipa"
echo "완료. 애플TV 설치는 GitHub 릴리즈 URL 로만 가능하다."
