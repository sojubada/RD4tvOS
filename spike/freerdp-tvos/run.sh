#!/usr/bin/env bash
# FreeRDP tvOS 스파이크 — 코드 0줄 검증
#
# 묻는 것은 하나다: libfreerdp + winpr 가 appletvos arm64 로 컴파일되는가.
# 앱 코드를 쓰기 전에, 기기 없이, 보드 없이 답을 낸다. 실패해도 손실이 없다.
#
# 플레이북 적용:
#   §2-4  선행 바이너리의 플랫폼은 문서가 아니라 Mach-O 로드 커맨드로 확인한다.
#   §4.6  조건부 코드는 호출 지점이 아니라 활성 조건을 읽는다 (H.264 가드).
#   §3.5  실패를 싸게 만든다 — make -k 로 한 번에 모든 오류를 모은다.
#
# 실패를 "성공"으로 위장하지 않는다. 각 단계의 결과를 그대로 보고서에 적는다.
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"
WORK="${WORK:-$PWD/spike-work}"
OUT="${OUT:-$PWD/spike-out}"
FREERDP_REF="${FREERDP_REF:-auto}"
DEPLOY_TARGET="${DEPLOY_TARGET:-16.0}"

mkdir -p "$WORK" "$OUT"
REPORT="$OUT/spike-report.md"
: > "$REPORT"

say()  { printf '\n=== %s ===\n' "$*"; }
note() { printf '%s\n' "$*" >> "$REPORT"; }

# .a 안의 Mach-O 로드 커맨드에서 플랫폼을 읽는다. 문서를 믿지 않는 지점.
macho_platform() {
  otool -lv "$1" 2>/dev/null | awk '/LC_BUILD_VERSION/{f=1} f&&/platform/{print $2; exit}'
}
macho_minos() {
  otool -lv "$1" 2>/dev/null | awk '/LC_BUILD_VERSION/{f=1} f&&/ minos /{print $2; exit}'
}

note "# FreeRDP tvOS 스파이크 보고서"
note ""
note "- 생성: $(date -u '+%Y-%m-%d %H:%M:%SZ')"
note "- 배포 타깃: tvOS $DEPLOY_TARGET"
note ""

# ---------------------------------------------------------------- 0. 환경
say "0. 환경"
{
  echo "## 0. 환경"
  echo ""
  echo '```'
  xcodebuild -version 2>&1 | sed 's/^/xcode: /'
  cmake --version 2>&1 | head -1 | sed 's/^/cmake: /'
  echo "sdks:"
  xcodebuild -showsdks 2>/dev/null | grep -i appletv | sed 's/^/  /'
  echo '```'
  echo ""
} >> "$REPORT"
xcodebuild -showsdks 2>/dev/null | grep -qi appletvos || {
  note "**중단: 이 러너에 appletvos SDK 가 없다.** 이미지를 바꿔야 한다."
  exit 1
}

# ------------------------------------------------- 1. ios-cmake 툴체인
say "1. ios-cmake 툴체인"
if [ ! -d "$WORK/ios-cmake" ]; then
  git clone --depth 1 https://github.com/leetal/ios-cmake "$WORK/ios-cmake" >/dev/null 2>&1
fi
TOOLCHAIN="$WORK/ios-cmake/ios.toolchain.cmake"
IOSCMAKE_SHA="$(git -C "$WORK/ios-cmake" rev-parse --short HEAD 2>/dev/null || echo '?')"
{
  echo "## 1. 툴체인"
  echo ""
  if [ -f "$TOOLCHAIN" ]; then
    echo "- leetal/ios-cmake \`$IOSCMAKE_SHA\` — TVOS 플랫폼 지원 여부:"
    echo '```'
    grep -nE '^\s*#\s*(TVOS|TVOSCOMBINED|SIMULATOR_TVOS)' "$WORK/ios-cmake/ios.toolchain.cmake" | head -5
    echo '```'
  else
    echo "- **실패: 툴체인 파일을 못 받았다.**"
  fi
  echo ""
} >> "$REPORT"
[ -f "$TOOLCHAIN" ] || exit 1

# ------------------------------------------------------- 2. FreeRDP 소스
say "2. FreeRDP 소스"
if [ "$FREERDP_REF" = "auto" ]; then
  # 가장 높은 3.x 태그를 고른다. 태그 이름을 추측하지 않는다.
  FREERDP_REF="$(git ls-remote --tags --refs https://github.com/FreeRDP/FreeRDP \
    | awk -F/ '{print $NF}' | grep -E '^3\.[0-9]+\.[0-9]+$' \
    | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)"
fi
echo "FreeRDP ref = $FREERDP_REF"
if [ ! -d "$WORK/FreeRDP" ]; then
  git clone --depth 1 --branch "$FREERDP_REF" https://github.com/FreeRDP/FreeRDP "$WORK/FreeRDP" >/dev/null 2>&1
fi
{
  echo "## 2. FreeRDP 소스"
  echo ""
  if [ -d "$WORK/FreeRDP" ]; then
    echo "- ref: \`$FREERDP_REF\` (commit \`$(git -C "$WORK/FreeRDP" rev-parse --short HEAD)\`)"
    echo "- 라이선스: 코어는 Apache-2.0, \`client/iOS\` 는 별도 → **코어만 쓴다**"
  else
    echo "- **실패: 클론 실패 (ref=$FREERDP_REF)**"
  fi
  echo ""
} >> "$REPORT"
[ -d "$WORK/FreeRDP" ] || exit 1

# ------------------------------------- 3. OpenSSL (tvOS arm64) 준비
say "3. OpenSSL tvOS arm64"
SSL_ROOT=""
if [ ! -d "$WORK/openssl-apple" ]; then
  git clone --depth 1 https://github.com/passepartoutvpn/openssl-apple "$WORK/openssl-apple" >/dev/null 2>&1
fi
if [ -d "$WORK/openssl-apple" ]; then
  # 타깃 이름을 추측하지 않고 --help 가 알려주는 목록에서 골라낸다.
  # 1차 스파이크는 여기서 시뮬레이터 타깃(tvos-sim-cross-arm64)을 집어 실패했다.
  # 기기 타깃만 고르도록 'sim' 을 배제하고, 옵션 플래그(--tvos-sdk 류)도 걸러낸다.
  ( cd "$WORK/openssl-apple" && ./build-libssl.sh --help ) > "$OUT/openssl-targets.txt" 2>&1
  SSL_TARGET="$(grep -oE '\btvos-[a-z0-9_-]*arm64\b' "$OUT/openssl-targets.txt" \
                | grep -v sim | sort -u | head -1)"
  echo "openssl target = ${SSL_TARGET:-<없음>}"
  if [ -n "$SSL_TARGET" ]; then
    ( cd "$WORK/openssl-apple" && ./build-libssl.sh --targets="$SSL_TARGET" ) \
      > "$OUT/openssl-build.log" 2>&1
    SSL_RC=$?
  else
    SSL_RC=90
  fi
  # 빌드물 중 플랫폼이 TVOS 인 libcrypto 를 찾아 OPENSSL_ROOT_DIR 를 조립한다.
  while IFS= read -r lib; do
    if [ "$(macho_platform "$lib")" = "TVOS" ]; then
      SSL_STAGE="$WORK/openssl-tvos"
      mkdir -p "$SSL_STAGE/lib" "$SSL_STAGE/include"
      cp "$lib" "$SSL_STAGE/lib/libcrypto.a"
      ssldir="$(dirname "$lib")"
      sslpeer="$(ls "$ssldir" | grep -i '^libssl' | head -1)"
      [ -n "$sslpeer" ] && cp "$ssldir/$sslpeer" "$SSL_STAGE/lib/libssl.a"
      inc="$(find "$WORK/openssl-apple" -type d -name openssl -path '*include*' | head -1)"
      [ -n "$inc" ] && cp -R "$(dirname "$inc")/." "$SSL_STAGE/include/"
      SSL_ROOT="$SSL_STAGE"
      break
    fi
  done < <(find "$WORK/openssl-apple" -name 'libcrypto*.a' 2>/dev/null)
fi
{
  echo "## 3. OpenSSL (tvOS arm64)"
  echo ""
  echo "- 선택한 타깃: \`${SSL_TARGET:-없음}\` (후보 목록은 \`openssl-targets.txt\`)"
  echo "- 빌드 종료코드: ${SSL_RC:-?}"
  if [ -n "$SSL_ROOT" ]; then
    echo "- OPENSSL_ROOT_DIR: \`$SSL_ROOT\`"
    for f in "$SSL_ROOT"/lib/*.a; do
      echo "  - \`$(basename "$f")\` platform=$(macho_platform "$f") minos=$(macho_minos "$f") arch=$(lipo -archs "$f" 2>/dev/null)"
    done
  else
    echo "- **TVOS 플랫폼 libcrypto 를 찾지 못했다.**"
    echo "- 1차 스파이크에서는 이 상태로 configure 가 통과했는데, CMake 가"
    echo "  homebrew 의 **macOS** libcrypto 를 집었기 때문이다. 거짓 통과를 막기 위해"
    echo "  이제 여기서 중단한다."
  fi
  echo ""
} >> "$REPORT"

if [ -z "$SSL_ROOT" ]; then
  note "## 중단"
  note ""
  note "tvOS OpenSSL 이 준비되지 않았다. 이 상태로 configure 하면 CMake 가 호스트(macOS)"
  note "라이브러리를 집어 **거짓 통과**가 난다 — 1차 스파이크에서 실제로 그랬다."
  cat "$REPORT"
  exit 1
fi

# ------------------------------------------------------- 4. configure
say "4. configure"
BUILD="$WORK/build-tvos"
rm -rf "$BUILD"
CFG_ARGS=(
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
  -DPLATFORM=TVOS -DIOS_PLATFORM=TVOS
  -DDEPLOYMENT_TARGET="$DEPLOY_TARGET"
  -DENABLE_BITCODE=OFF -DENABLE_ARC=ON
  -DCMAKE_BUILD_TYPE=Release
  -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF
  # 코어만 짓는다. 클라이언트·서버·샘플은 iOS 전제라 건드리지 않는다(§3.2).
  -DWITH_CLIENT=OFF -DWITH_CLIENT_COMMON=OFF -DWITH_CLIENT_SDL=OFF
  -DWITH_SERVER=OFF -DWITH_SAMPLE=OFF -DWITH_PROXY=OFF -DWITH_SHADOW=OFF
  # dlopen 을 피한다 — tvOS 에서 가장 먼저 막힐 자리다.
  -DBUILTIN_CHANNELS=ON
  -DWITH_WINPR_TOOLS=OFF -DWITH_MANPAGES=OFF
  -DWITH_KRB5=OFF -DWITH_PKCS11=OFF -DWITH_PCSC=OFF
  -DWITH_X11=OFF -DWITH_WAYLAND=OFF -DWITH_CUPS=OFF
  -DWITH_ALSA=OFF -DWITH_PULSE=OFF -DWITH_OSS=OFF
  -DWITH_FFMPEG=OFF -DWITH_SWSCALE=OFF -DWITH_CAIRO=OFF
  -DWITH_OPENH264=OFF -DWITH_LIBSYSTEMD=OFF -DWITH_SOXR=OFF
  -DWITH_OPENSSL=ON -DOPENSSL_USE_STATIC_LIBS=ON
  # 1차 결과 반영: json-c 헤더를 못 찾아 winpr/utils/json 이 깨졌다. 코어 RDP 에 불필요하다.
  -DWITH_JSON_DISABLED=ON
  # 1차 결과 반영: process.c 의 fork/execve 가 tvOS 에서 unavailable.
  # 벤더 소스를 고치지 않고 강제 포함 헤더로 실패 스텁을 넣는다.
  -DCMAKE_C_FLAGS="-include $ROOT/tvos_shim.h"
  -DCMAKE_CXX_FLAGS="-include $ROOT/tvos_shim.h"
  -DCMAKE_OBJC_FLAGS="-include $ROOT/tvos_shim.h"
)
[ -n "$SSL_ROOT" ] && CFG_ARGS+=( -DOPENSSL_ROOT_DIR="$SSL_ROOT" -DOPENSSL_INCLUDE_DIR="$SSL_ROOT/include" )
cmake -S "$WORK/FreeRDP" -B "$BUILD" "${CFG_ARGS[@]}" > "$OUT/configure.log" 2>&1
CFG_RC=$?
echo "configure rc = $CFG_RC"
{
  echo "## 4. configure"
  echo ""
  echo "- 종료코드: **$CFG_RC** ($([ $CFG_RC -eq 0 ] && echo 통과 || echo 실패))"
  if [ $CFG_RC -ne 0 ]; then
    echo ""
    echo '```'
    grep -E 'CMake Error|Could NOT find|error' "$OUT/configure.log" | head -25
    echo '```'
  fi
  echo ""
} >> "$REPORT"

# 실제 옵션 이름을 추측하지 않고 캐시에서 읽어 둔다. H.264 경로 판단에 쓴다.
if [ -d "$BUILD" ]; then
  cmake -LAH "$BUILD" > "$OUT/cmake-cache.txt" 2>&1
  {
    echo "### 코덱·암호 관련 실제 옵션"
    echo ""
    echo '```'
    grep -iE '^(WITH_)?(H264|VIDEOTOOLBOX|OPENH264|MEDIACODEC|DSP|OPENSSL|MBEDTLS|ZLIB|SIMD|NEON)[A-Z_]*:' \
      "$OUT/cmake-cache.txt" | head -20
    echo '```'
    echo ""
  } >> "$REPORT"
fi

# --------------------------------------------------------- 5. 빌드
say "5. 빌드"
BUILD_RC=0
if [ $CFG_RC -eq 0 ]; then
  # -k: 첫 오류에서 멈추지 않는다. 한 번에 모든 결손을 세는 것이 이 스파이크의 목적이다.
  cmake --build "$BUILD" --config Release -- -k -j3 > "$OUT/build.log" 2>&1
  BUILD_RC=$?
else
  echo "configure 실패로 빌드 생략" > "$OUT/build.log"
  BUILD_RC=$CFG_RC
fi
echo "build rc = $BUILD_RC"

ERRS=$(grep -cE 'error:' "$OUT/build.log" 2>/dev/null || echo 0)
{
  echo "## 5. 빌드"
  echo ""
  echo "- 종료코드: **$BUILD_RC**"
  echo "- \`error:\` 줄 수: **$ERRS**"
  echo ""
  if [ "$ERRS" -gt 0 ]; then
    echo "### 오류 (중복 제거 상위 30)"
    echo ""
    echo '```'
    grep -E 'error:' "$OUT/build.log" | sed 's/^.*error: //' | sort | uniq -c | sort -rn | head -30
    echo '```'
    echo ""
  fi
} >> "$REPORT"

# ------------------------------ 6. POSIX 결손 집계 (불확실성 #1)
say "6. POSIX 결손"
{
  echo "## 6. POSIX 결손 집계 (불확실성 #1)"
  echo ""
  echo "tvOS 에서 막히거나 불안정한 호출이 오류로 나타난 건수. 0 이 아니면 패치 범위를 여기서 센다."
  echo ""
  echo "| 심볼 | 오류 언급 |"
  echo "|---|---|"
  for sym in fork vfork posix_spawn execve execvp 'system' waitpid dlopen dlsym popen \
             getpwuid getpwnam sysctl ptrace mkfifo shm_open pthread_cancel; do
    n=$(grep -E 'error:' "$OUT/build.log" 2>/dev/null | grep -cw "$sym" || true)
    [ "${n:-0}" -gt 0 ] && echo "| \`$sym\` | $n |"
  done
  echo ""
} >> "$REPORT"

# ------------------ 7. H.264 디코더 활성 조건 (불확실성 #3)
say "7. H.264 활성 조건"
{
  echo "## 7. H.264 디코더 활성 조건 (불확실성 #3)"
  echo ""
  echo "플레이북 §4.6 — 호출 지점이 아니라 **분기 조건**이 사실이다."
  echo ""
  echo '```'
  grep -rnE 'TARGET_OS_(OSX|MAC|IPHONE|TV)|VideoToolbox|VTDecompression' \
    "$WORK/FreeRDP/libfreerdp/codec" 2>/dev/null | head -25
  echo '```'
  echo ""
  echo "VideoToolbox 관련 CMake 조건:"
  echo '```'
  grep -rnE 'VideoToolbox|VIDEOTOOLBOX' "$WORK/FreeRDP" --include=CMakeLists.txt \
    --include='*.cmake' 2>/dev/null | head -15
  echo '```'
  echo ""
} >> "$REPORT"

# -------------------- 8. 산출물 플랫폼 확인 (불확실성 #2)
say "8. 산출물"
mkdir -p "$OUT/libs"
FOUND=0
TVOS_OK=0
while IFS= read -r a; do
  FOUND=$((FOUND+1))
  cp "$a" "$OUT/libs/" 2>/dev/null
done < <(find "$BUILD" -name 'lib*.a' 2>/dev/null | grep -Ei 'freerdp|winpr')
{
  echo "## 8. 산출물 플랫폼 확인 (불확실성 #2)"
  echo ""
  echo "- 찾은 정적 라이브러리: **$FOUND 개**"
  echo ""
  if [ "$FOUND" -gt 0 ]; then
    echo "| 라이브러리 | arch | platform | minos |"
    echo "|---|---|---|---|"
    for f in "$OUT"/libs/*.a; do
      p="$(macho_platform "$f")"
      [ "$p" = "TVOS" ] && TVOS_OK=$((TVOS_OK+1))
      echo "| \`$(basename "$f")\` | $(lipo -archs "$f" 2>/dev/null) | ${p:-?} | $(macho_minos "$f") |"
    done
    echo ""
    echo "- platform=TVOS 로 확인된 것: **$TVOS_OK / $FOUND**"
  fi
  echo ""
} >> "$REPORT"

# ------------- 8.5 VideoToolbox tvOS 분기 추가 시도 (불확실성 #3)
say "8.5 VideoToolbox"
VT_PATCH="미시도"
VT_CFG_RC="-"
VT_BUILD_RC="-"
VT_OBJ="없음"
VTFILE="$WORK/FreeRDP/cmake/ConfigOptionsVideoToolbox.cmake"
if [ "$FOUND" -gt 0 ] && [ -f "$VTFILE" ]; then
  VT_PATCH="$(python3 "$ROOT/patch_videotoolbox.py" "$VTFILE" 2>&1)"
  if [ "$VT_PATCH" = "패치됨" ] || [ "$VT_PATCH" = "이미적용" ]; then
    BUILD_VT="$WORK/build-tvos-vt"
    rm -rf "$BUILD_VT"
    cmake -S "$WORK/FreeRDP" -B "$BUILD_VT" "${CFG_ARGS[@]}" \
      -DWITH_APPLE_VIDEOTOOLBOX=ON > "$OUT/configure-vt.log" 2>&1
    VT_CFG_RC=$?
    if [ $VT_CFG_RC -eq 0 ]; then
      cmake --build "$BUILD_VT" --config Release -- -k -j3 > "$OUT/build-vt.log" 2>&1
      VT_BUILD_RC=$?
      found_obj="$(find "$BUILD_VT" -name 'h264_videotoolbox*.o' 2>/dev/null | head -1)"
      [ -n "$found_obj" ] && VT_OBJ="$(basename "$found_obj")"
    fi
  fi
fi
{
  echo "## 8.5 VideoToolbox — tvOS 분기 추가 시도 (불확실성 #3)"
  echo ""
  echo "상류 게이트는 CMAKE_SYSTEM_NAME 이 Darwin(macOS) 또는 iOS 일 때만 켠다."
  echo "tvOS 는 두 분기 어디에도 걸리지 않아 기본 OFF 이고, 강제로 켜면 FATAL_ERROR 가 난다."
  echo "**코드 제약이 아니라 CMake 조건이다** — 디코더가 쓰는 VideoToolbox API 는 tvOS SDK 에 있다."
  echo ""
  echo "| 항목 | 결과 |"
  echo "|---|---|"
  echo "| 조건 패치 | $VT_PATCH |"
  echo "| configure | $VT_CFG_RC |"
  echo "| build | $VT_BUILD_RC |"
  echo "| h264_videotoolbox 오브젝트 | $VT_OBJ |"
  echo ""
  if [ -f "$OUT/build-vt.log" ] && grep -qE 'error:' "$OUT/build-vt.log"; then
    echo "오류:"
    echo '```'
    grep -E 'error:' "$OUT/build-vt.log" | sed 's/^.*error: //' | sort -u | head -15
    echo '```'
    echo ""
  fi
  echo "H.264 는 RDP 의 **선택적** 코덱이다(RemoteFX·NSCodec·Planar 로 폴백된다)."
  echo "여기서 실패해도 RDP 자체는 성립하고, 4K 성능에만 영향이 있다."
  echo ""
} >> "$REPORT"

# ------------------------------------------------------- 9. 판정
say "9. 판정"
{
  echo "## 9. 판정"
  echo ""
  if [ "$FOUND" -gt 0 ] && [ "$TVOS_OK" -eq "$FOUND" ] && [ "$BUILD_RC" -eq 0 ]; then
    echo "**A안 진행 가능.** 코어가 tvOS 로 빌드되고 모든 산출물이 TVOS 플랫폼으로 확인됐다."
    echo ""
    echo "다음: UI 없이 접속 시험(핸드셰이크 + NLA)만 하는 최소 타깃 → 그 다음 TV 화면 작성."
  elif [ "$FOUND" -gt 0 ]; then
    echo "**부분 성공.** 일부가 빌드됐다. §6 결손 표의 건수로 패치 범위를 판단한다."
    echo ""
    echo "- 결손이 소수이고 winpr 한두 파일에 몰려 있으면 A안 계속."
    echo "- 광범위하면 폴백으로 전환."
  elif [ "$ERRS" -le 10 ]; then
    echo "**유망하나 미완.** 산출물은 없지만 오류가 $ERRS 건뿐이다."
    echo ""
    echo "오류가 적고 한두 파일에 몰려 있으면 이것은 포팅 불가가 아니라 설정·스텁 문제다."
    echo "§5 오류 목록과 §6 결손 표로 다음 수를 정하고, 폴백으로 넘어가기 전에 한 번 더 돌린다."
  else
    echo "**실패.** 오류가 $ERRS 건으로 광범위하다. 폴백으로 전환한다."
    echo ""
    echo "앱 코드를 쓰지 않았으므로 손실은 CI 시간뿐이다."
  fi
  echo ""
  echo "---"
  echo ""
  echo "로그: \`configure.log\`, \`build.log\`, \`openssl-build.log\`, \`cmake-cache.txt\`"
} >> "$REPORT"

cat "$REPORT"
exit 0
