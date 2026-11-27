# FreeRDP tvOS 스파이크 — 읽는 법

## 왜 이걸 먼저 하는가

윈도우를 **별도 세션으로 로그인**해서 쓰려면 RDP가 필수다. 화면 공유 계열(Sunshine+Moonlight,
VNC 서버)은 PC의 현재 세션을 미러링하므로 그 요구를 만족하지 못한다.

그런데 RDP를 tvOS에 올리는 일에는 코드를 쓰기 전에 알 수 없는 것이 세 가지 있었다.
이 스파이크는 **앱 코드를 한 줄도 쓰지 않고** 그 셋에 답한다. 기기도 보드도 필요 없다.

## 세 가지 불확실성

| # | 질문 | 보고서 절 |
|---|---|---|
| 1 | winpr 가 tvOS 에서 막히는 POSIX 호출(`fork`·`dlopen` 등)에 얼마나 의존하는가 | §6 |
| 2 | 빌드물이 실제로 **appletvos 슬라이스**인가 (문서를 믿지 않는다) | §8 |
| 3 | H.264 디코더의 **활성 조건**이 macOS 전용인가 | §7 |

## 돌리는 법

```
gh api -X POST repos/sojubada/RD4tvOS/actions/workflows/freerdp-tvos-spike.yml/dispatches \
  -f ref=main -f 'inputs[freerdp_ref]=' -f 'inputs[deployment_target]=16.0'
```

결과는 Actions 요약 화면에 바로 뜨고, `freerdp-tvos-spike` 아티팩트에 전체 로그가 담긴다.

## 스파이크가 하는 일

1. 러너에 `appletvos` SDK 가 있는지 확인. 없으면 즉시 중단한다.
2. leetal/ios-cmake 를 받아 `TVOS` 플랫폼 지원 줄을 보고서에 그대로 찍는다.
3. FreeRDP 최신 3.x 태그를 **자동으로 골라** 받는다(태그 이름을 추측하지 않는다).
4. openssl-apple 로 tvOS arm64 OpenSSL 을 짓는다. 타깃 이름은 `--help` 출력에서 찾아낸다.
5. 코어만 configure — 클라이언트·서버·샘플 전부 OFF, `BUILTIN_CHANNELS=ON` 으로 dlopen 회피.
6. `make -k` 로 빌드. **첫 오류에서 멈추지 않는다** — 한 번에 모든 결손을 세는 것이 목적이다.
7. 오류 로그에서 POSIX 심볼 언급 건수를 집계한다.
8. 산출된 `.a` 마다 `otool -lv` 로 `LC_BUILD_VERSION` 의 platform 을 읽는다.

## 판정 기준

| 보고서 §9 | 다음 조치 |
|---|---|
| 빌드 성공 + 전 산출물 platform=TVOS | **A안 확정.** UI 없이 접속 시험(핸드셰이크+NLA)만 하는 최소 타깃 → TV 화면 작성 |
| 부분 성공 | §6 결손 건수로 판단. 소수이고 winpr 한두 파일에 몰려 있으면 A안 계속, 광범위하면 폴백 |
| 실패 | 폴백으로 전환. 앱 코드를 쓰지 않았으므로 손실은 CI 시간뿐 |

### 폴백 선택 주의

폴백을 **D안(Sunshine + 기존 Moonlight TV)** 으로 지정해 두었다. 코드가 0줄이고 체감 품질이
가장 좋다. 다만 **별도 세션 로그인이 되지 않는다** — PC 현재 세션의 미러링이다.
별도 세션이 양보할 수 없는 요구면 폴백은 **B안(NAS를 RDP 게이트웨이로)** 이어야 한다.
B안은 RDP 세션을 그대로 쓰면서 애플TV는 VNC만 구현하므로 맥 경로와 코드를 공유한다.
NAS는 i3-4130 쪽을 쓴다(RDP 디코딩 + VNC 재인코딩은 단일 스레드 성능이 지배).

## 스파이크가 답하지 않는 것

- 실제 접속이 되는가 (NLA 인증, 채널 협상) — 다음 단계
- 성능 — 그 다음
- 앱 UI·입력 — 3·4단계

---

# 결과 (2026-11-27) — A안 진행 가능

4회 반복해 전부 통과했다. 매 회 2분, 기기도 보드도 쓰지 않았다.

## 최종 상태

| 항목 | 결과 |
|---|---|
| 빌드 종료코드 | **0**, `error:` **0건** |
| `libfreerdp3.a` | arm64 / **platform=TVOS** / minos 16.0 — 오브젝트 188개 |
| `libwinpr3.a` | arm64 / **platform=TVOS** / minos 16.0 — 오브젝트 145개 |
| POSIX 결손 | **0건** |
| VideoToolbox H.264 | `h264_videotoolbox.m.o` 컴파일 성공 |
| 환경 | Xcode 16.0 / tvOS 18.0 SDK / CMake 4.5.1 / FreeRDP 3.33.1 / OpenSSL 3.4.1 |

산출물 플랫폼은 CI 보고서와 별도로 아카이브 멤버의 `LC_BUILD_VERSION` 을 직접 파싱해
재확인했다 — 333개 전부 TVOS 16.0 이다(§2-4, 문서를 믿지 않는다).

## 세 불확실성에 대한 답

**1. winpr 의 POSIX 의존 — 결론: 두 곳뿐이다.**
`winpr/libwinpr/thread/process.c` 의 `fork`(255행)와 `execve`(426행), 둘 다 한 파일의
`CreateProcess` 구현 안이다. RDP 클라이언트는 자식 프로세스를 만들지 않으므로 실패 스텁으로
충분하다. `fork` 가 -1 을 돌려주면 그 함수는 그대로 `goto finish` 로 빠져 실패를 반환한다.
즉 "tvOS 에서 프로세스 생성은 지원되지 않는다"는 정확한 동작이 된다.
벤더 소스는 고치지 않았다 — 강제 포함 헤더(`tvos_shim.h`)로 넣는다.

**2. 플랫폼 슬라이스 — 결론: 진짜 tvOS 다.** 단 함정이 하나 있었다.
3차에서 `libfreerdp3.a` 의 멤버가 전부 `0b17c0de`(LLVM 비트코드)로 나왔다.
`CMAKE_INTERPROCEDURAL_OPTIMIZATION=ON` 이어서 LTO 아카이브가 됐고, 비트코드에는
`LC_BUILD_VERSION` 이 없어 **플랫폼을 확인할 방법이 없었다.** 빌드 실패가 아니라 확인 불가다.
LTO 를 끄자 실제 Mach-O 가 나왔다. 앱에서는 LTO 를 다시 켜도 되지만, 그때는 링크 시험이 증거다.

**3. H.264 활성 조건 — 결론: 코드 제약이 아니라 CMake 조건 한 줄이었다.**
`cmake/ConfigOptionsVideoToolbox.cmake` 의 게이트가 `CMAKE_SYSTEM_NAME` 이 `Darwin` 또는
`iOS` 일 때만 켠다. tvOS 는 두 분기 어디에도 걸리지 않아 기본 OFF 이고, 강제로 켜면
`FATAL_ERROR` 가 난다. 그런데 디코더(`h264_videotoolbox.m`)가 쓰는
`VTDecompressionSession`·`CMSampleBuffer` API 는 tvOS SDK 에 있다.
조건에 tvOS 분기를 더하자 바로 컴파일됐다. 상류에 제안할 만한 변경이다.

덧붙여, 이것은 **선택적 기능**이지 초기화 전제조건이 아니다(§4.4). 실패해도 RDP 는
RemoteFX·NSCodec·Planar 로 성립한다. PDFKit 사례와 다른 위치다.

## 재현 레시피 (다음 단계가 그대로 쓸 설정)

```
toolchain : leetal/ios-cmake, -DPLATFORM=TVOS -DDEPLOYMENT_TARGET=16.0 -DENABLE_BITCODE=OFF
openssl   : openssl-apple --targets=tvos-cross-arm64 --deprecated --min-tvos-sdk=16.0
freerdp   : -DWITH_CLIENT=OFF -DWITH_SERVER=OFF -DWITH_SAMPLE=OFF -DWITH_PROXY=OFF -DWITH_SHADOW=OFF
            -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DBUILTIN_CHANNELS=ON
            -DWITH_JSON_DISABLED=ON -DWITH_KRB5=OFF -DWITH_WINPR_TOOLS=OFF
            -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
            -DCMAKE_C_FLAGS="-include spike/freerdp-tvos/tvos_shim.h"
videotoolbox : cmake/ConfigOptionsVideoToolbox.cmake 에 tvOS 분기 추가 후 -DWITH_APPLE_VIDEOTOOLBOX=ON
```

함정 둘을 기록해 둔다.

- **OpenSSL 타깃 이름.** `tvos-sim-cross-arm64` 는 시뮬레이터다. 기기용은 `tvos-cross-arm64` 다.
  1차에서 시뮬레이터를 집었는데, 그때 CMake 가 **homebrew 의 macOS libcrypto** 를 집어
  configure 가 "통과"했다. 거짓 통과다. 지금은 tvOS libcrypto 가 없으면 configure 전에 중단한다.
- **`--deprecated` 가 필요하다.** openssl-apple 의 기본값은 `no-deprecated` 이고, OpenSSL 3.x 는
  `SSL_get_peer_certificate` 를 헤더의 호환 매크로로만 둔다. 그래서 그 매크로가 사라져
  `libfreerdp/crypto/tls.c:730` 이 깨진다. 심볼 자체(`SSL_get1_peer_certificate`)는 라이브러리에 있다.

## 다음 단계

1. **최소 접속 시험** — UI 없이 핸드셰이크 + NLA 인증만. 여기서 winpr 의 SSPI/NTLM 경로가
   tvOS 에서 실제로 도는지 본다. 컴파일과 동작은 다른 문제다.
2. 통과하면 RD4tvOS 앱에 코어를 넣고 TV 화면을 새로 쓴다(§3.2 — 상류를 패치하지 않는다).
3. 입력은 프로토콜과 분리한 공통 이벤트 계층으로 받는다 — 마우스·키보드·조이스틱 3종 상시 유효.
