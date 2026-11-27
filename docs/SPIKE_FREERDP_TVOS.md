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
