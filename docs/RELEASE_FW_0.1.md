# RELEASE_FW_0.1 — x1bridge 0.1.0 (1단계: 가상 델타)

## 목적

RD4tvOS 앱 코드를 한 줄도 쓰기 전에 "ESP32-C3 → 애플TV" 경로만 검증한다.
성공 판정은 단순하다 — **기존 Moonlight TV에서 커서가 원을 그리는가.**

## 기능별 커밋

| 기능 | 파일 |
|---|---|
| DiagLog 로거 (카테고리·레벨·traceId·domain/code/msg·링버퍼) | `firmware/x1bridge/main/diaglog.{h,c}` |
| X1 호환 GATT 서버 (서비스 + 0x2A4E + 0x2A4D×2 + 0x2908) | `firmware/x1bridge/main/x1_profile.{h,c}` |
| 가상 델타 패턴 생성기 (circle/hsweep/step) | `firmware/x1bridge/main/vsrc.{h,c}` |
| 상태 LED + 시리얼 콘솔 + 하트비트 | `firmware/x1bridge/main/app_main.c` |
| CI 빌드 (맥 툴체인 없이) | `.github/workflows/firmware.yml` |

## 끄는 스위치 (재빌드 없이)

| 대상 | 방법 |
|---|---|
| 패턴 전송 중지 | 시리얼 `p` 로 `off` 까지 돌린다 |
| 로그 상세도 | 시리얼 `+` / `-`, `ble.gatt` 만 `g` |
| 리포트 한 줄씩 보기 | `g` → `ble.gatt` TRACE |

## 재빌드가 필요한 스위치

| 대상 | 위치 |
|---|---|
| 광고 이름 | `x1_profile.h` 의 `X1_DEV_NAME`. Moonlight가 이름으로 걸러내면 `"Citrix X1 Mouse"` 로 바꾼다 |
| LED 핀 | `app_main.c` 의 `LED_GPIO` (SuperMini 기준 8, active-low) |
| 패턴 크기·속도 | `vsrc.c` 의 `VSRC_RADIUS_PX`, `VSRC_PERIOD_S`, `VSRC_SWEEP_PX` |
| 버튼/휠 리포트 형식 | `x1_profile.c` 의 `x1_send_btn_wheel` — 현재 3바이트형, 필요시 5바이트형 |

## 되돌리는 순서

이 릴리즈는 신규 파일만 추가한다(저장소가 비어 있었다). 되돌릴 때는
`firmware/` 와 `.github/workflows/firmware.yml` 을 함께 제거하면 원상태다.
애플TV 앱 코드는 아직 없으므로 영향 범위가 없다.

## 기기에서 볼 것 (체크리스트)

1. 보드에 전원 → LED 0.5초 점멸 시작 (= 광고 중).
2. 애플TV에서 Moonlight TV 설정의 X1 마우스를 켠다.
3. LED가 계속 켜짐으로 바뀐다 (= 연결 + 구독).
4. 커서가 약 4초에 한 바퀴 원을 그린다.
5. 시리얼 `s` 로 숫자 확인:
   - `notify xy` 가 초당 약 100씩 증가하는가
   - `conn interval` 이 몇 ms로 협상됐는가 (애플 하한 15ms)
   - `clamped` 가 0인가
   - `boot mode` 가 1인가 (= central이 0x2A4E에 0x00을 썼는가)
6. 전원을 뽑고 다시 꽂아 재연결되는지 확인.

## 빌드 결과 (CI)

| 항목 | 값 |
|---|---|
| ESP-IDF / 타깃 | v5.4 / esp32c3 |
| 앱 크기 | 0x7BC10 (506,896 B) — 앱 파티션 52% 여유 |
| 부트로더 | 0x5120 (37% 여유) |
| merged.bin | 572,432 B — **offset 0x0 에 굽는다** |
| 경고 | 없음 (`-Werror=all` 통과) |

## 측정 기록 (실기기 확인 후 채운다)

| 항목 | 값 | 비고 |
|---|---|---|
| Moonlight TV 인식 여부 | | 이름으로 거르는지 UUID로 거르는지 판정 |
| 협상된 conn interval | | ms |
| notify xy / 5초 | | 기대 약 500 |
| notify err | | 0 이어야 한다 |
| clamped | | 0 이어야 한다 |
| boot mode write | | 있었는가 |
| 체감 지연 | | 원 궤적이 끊기는가 |

## 실패 시 분기

| 결과 | 다음 조치 |
|---|---|
| Moonlight가 장치를 못 찾음 | `X1_DEV_NAME` 을 `"Citrix X1 Mouse"` 로 바꿔 재빌드(1회) |
| 연결되나 구독 없음 | 0x2908 디스크립터 값·순서 재확인 |
| 전부 안 됨 | **BLE 경로를 접고 WiFi UDP 경로를 1순위로 올린다** (전략 문서 §4.4). 앱 코드는 아직 없으므로 손실이 없다 |
