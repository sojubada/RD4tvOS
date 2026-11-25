# x1bridge — Citrix X1 호환 BLE 중계기 (ESP32-C3)

애플TV에서 마우스를 쓰기 위한 중계기 펌웨어. tvOS는 포인팅 장치를 시스템 차원에서
지원하지 않으므로, 앱이 CoreBluetooth central로 붙어 커스텀 서비스에서 좌표를 직접 받는
방식만 성립한다. X1 마우스가 쓰는 프로파일을 그대로 흉내내므로 **기존 Moonlight TV로도
동작을 확인할 수 있다** — 이것이 RD4tvOS 앱 코드를 쓰기 전 0단계 검증의 핵심이다.

## 1단계(현재): 가상 델타 패턴

마우스 없이, 앱 코드 없이 "ESP32 → 애플TV" 경로만 검증한다.
펌웨어가 100Hz로 원운동 좌표를 쏘므로 **애플TV 화면에서 커서가 원을 그리면 통과**다.

버튼 클릭은 보내지 않는다(원격 세션에서 아무 곳이나 클릭하면 위험하다). 휠도 꺼져 있다.

## 빌드

로컬 빌드를 하지 않는다. `firmware/**` 를 푸시하면 GitHub Actions가 빌드하고
`x1bridge-firmware` 아티팩트에 `x1bridge-merged.bin` 을 올린다.

수동 실행:

```
gh api -X POST repos/sojubada/rd4tvos/actions/workflows/firmware.yml/dispatches -f ref=main
```

## 굽기 (맥에 툴체인 설치 없이)

1. 아티팩트에서 `x1bridge-merged.bin` 을 내려받는다.
2. 크롬에서 <https://espressif.github.io/esptool-js/> 를 연다.
3. 보드를 USB-C로 맥에 연결하고 **Connect** → 포트 선택.
4. `x1bridge-merged.bin` 을 **offset 0x0** 으로 지정하고 **Program**.
5. 같은 페이지의 콘솔에서 로그가 보인다(보드 내장 USB Serial/JTAG).

부팅이 안 되면 BOOT 버튼을 누른 상태로 연결해 다운로드 모드로 들어간다.

## 기기에서 볼 것

내장 LED(GPIO8):

| LED | 의미 |
|---|---|
| 0.5초 점멸 | 광고 중 — 아무도 붙지 않았다 |
| 0.1초 점멸 | 연결됐으나 구독 안 됨 — 앱이 notify를 켜지 않았다 |
| 계속 켜짐 | 연결 + 구독 완료 — 리포트 전송 중 |
| 꺼짐 | 호스트 미동기 또는 오류 |

## 시리얼 단일문자 명령 (재빌드 없이 조절)

| 키 | 동작 |
|---|---|
| `h` | 도움말 |
| `s` | 상태·통계 (연결 구간, 알림 수, 포화 횟수, 부트모드 여부) |
| `d` | DiagLog 링버퍼 덤프 (최근 200줄) |
| `c` | 링버퍼 비우기 |
| `p` | 패턴 전환 `off → circle → hsweep → step` |
| `+` / `-` | 전체 로그 레벨 올리기 / 내리기 |
| `g` | `ble.gatt` 만 TRACE 토글 — 리포트를 한 줄씩 본다 |
| `R` | 재부팅 |

## 막혔을 때 어디를 보는가

| 증상 | DiagLog에서 확인 | 해석 |
|---|---|---|
| 애플TV가 장치를 못 찾는다 | `ble.adv  advertising name=...` 이 찍히는가 | 광고 자체가 안 나가면 `adv_start failed` 의 code를 본다 |
| 연결은 되는데 커서가 안 움직인다 | `ble.gatt subscribe XY notify=1` | 구독이 없으면 앱이 0x2A4D를 못 찾은 것 → 0x2908 디스크립터 확인 |
| 구독까지 됐는데 안 움직인다 | `s` 의 `notify xy` 증가 여부 | 증가하는데 안 움직이면 앱 쪽 좌표 변환 문제 |
| `protocol-mode write` 가 없다 | `ble.gatt protocol-mode write val=0x00` | central이 직결 모드로 전환하지 않았다. 동작에 꼭 필요한지 이 로그로 판정한다 |
| 커서가 느리거나 끊긴다 | `ble.link conn-update itvl=` | 애플 플랫폼 하한은 15ms. 그보다 크면 지연의 원인이다 |
| 빠르게 움직일 때 끌린다 | `s` 의 `clamped` | 0이 아니면 12비트 범위를 넘긴 것 |

## 다음 단계

실제 마우스(Microsoft Surface/Modern Mobile Mouse, Model 1679/1679C)를 central로 붙인다.
BLE 4.2 LE / HOGP 장치이므로 추가로 필요한 것:

1. LE Secure Connections 본딩 — HOGP는 암호화 없이 리포트 알림을 주지 않는다.
2. IRK 기반 RPA 주소 해석 — 없으면 전원을 끈 뒤 다시 붙지 않는다.
3. Report Map(0x2A4B) 파싱 — 1800 DPI 장치라 X/Y가 16비트일 수 있다. 고정 레이아웃 가정 금지.
4. 스캔·광고 상태기계 — 라디오를 동시 점유할 수 없으므로 서로 멈춰 줘야 한다.

세부는 `docs/` 와 프로젝트 문서 `claude/rd4tvos-strategy.md` 를 본다.
