// x1_profile.h — Citrix X1 마우스 호환 BLE peripheral (GATT 서버)
//
// 왜 X1 호환인가:
//   tvOS는 포인팅 장치를 시스템 차원에서 지원하지 않고, CoreBluetooth는 central 역할만
//   제공한다(동시 2개, 백그라운드 불가). 따라서 애플TV 앱이 central로 붙어 커스텀 서비스에서
//   좌표를 직접 읽는 방식만 성립한다. X1 마우스가 쓰는 프로파일을 그대로 흉내내면
//   ① 이미 설치된 Moonlight TV로 앱 코드 없이 경로 전체를 검증할 수 있고
//   ② MIT 라이선스 구현체(fizzyade/X1Kit)를 RD4tvOS에 그대로 쓸 수 있다.
//
// 프로파일 명세:
//   Service  2B080000-BDB5-F6EB-24AE-9D6AB282AB63  (128-bit 커스텀)
//    ├ 0x2A4E Protocol Mode    read/write. central이 0x00(Boot Protocol)을 1바이트 write
//    ├ 0x2A4D Report (XY)      read/notify + 0x2908 디스크립터 = {0x02,0x01}
//    └ 0x2A4D Report (btn/wh)  read/notify + 0x2908 디스크립터 = {0x01,0x01}
//
// 지켜야 할 3가지 (문서 claude/rd4tvos-strategy.md §4.3):
//   1. 0x1812(HID over GATT)를 광고하지 않는다. 애플TV가 시스템 HID로 붙으려다 실패한다.
//   2. X1 특성에 암호화·본딩을 요구하지 않는다. tvOS 설정에서 페어링하지 않고 앱이 바로 붙는다.
//   3. 0x2908 디스크립터를 실제로 구현한다. 빠지면 조용히 아무것도 움직이지 않는다.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 광고 이름. Moonlight가 서비스 UUID가 아니라 이름으로 걸러낸다면
// "Citrix X1 Mouse"로 바꿔 다시 빌드한다(0단계에서 판정할 항목).
#define X1_DEV_NAME "X1 Mouse"

// 12비트 부호 델타의 표현 범위
#define X1_DELTA_MIN (-2048)
#define X1_DELTA_MAX (2047)

typedef struct {
    bool     connected;
    bool     xy_subscribed;
    bool     btn_subscribed;
    bool     boot_mode;        // central이 0x2A4E에 0x00을 썼는가
    uint16_t conn_itvl_125us;  // 협상된 연결 구간 (1.25ms 단위)
    uint16_t mtu;
    uint32_t notify_xy;
    uint32_t notify_btn;
    uint32_t notify_err;
    uint32_t clamped;          // 12비트 범위를 넘겨 잘린 횟수
    uint32_t conn_count;
} x1_status_t;

void x1_init(void);
void x1_get_status(x1_status_t *out);
bool x1_ready_for_xy(void);   // 연결 + XY 구독 완료

// 성공 0, 미연결/미구독 -1, mbuf 실패 -2, 알림 실패는 음수 NimBLE rc
int x1_send_xy(int dx, int dy);
int x1_send_btn_wheel(uint8_t buttons, int wheel);

#ifdef __cplusplus
}
#endif
