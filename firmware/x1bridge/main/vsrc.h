// vsrc.h — 가상 델타 패턴 생성기 (0단계 검증용)
//
// 왜 필요한가: 마우스가 도착하기 전에, 그리고 RD4tvOS 앱 코드를 한 줄도 쓰기 전에
// "ESP32 → 애플TV" 경로만 따로 검증하기 위한 것이다. 보드를 꽂고 기존 Moonlight TV에서
// 커서가 원을 그리면 BLE·GATT·좌표 패킹·tvOS 수신까지 전부 통과한 것이다.
//
// 안전: 버튼 클릭은 보내지 않는다. 원격 세션에서 아무 곳이나 클릭하면 위험하다.
// 휠도 기본 꺼짐이다.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VSRC_OFF = 0,
    VSRC_CIRCLE,   // 반지름 R 원운동 — 커서가 도는지로 전 구간 확인
    VSRC_HSWEEP,   // 좌우 왕복 — 한 방향 포화·누락 확인
    VSRC_STEP,     // 1초에 한 번 고정 10px — 1:1 환산 보정값 측정
    VSRC_MODE_COUNT
} vsrc_mode_t;

void        vsrc_start(void);              // 태스크 기동
void        vsrc_set_mode(vsrc_mode_t m);
vsrc_mode_t vsrc_get_mode(void);
const char *vsrc_mode_name(vsrc_mode_t m);
void        vsrc_cycle_mode(void);
void        vsrc_get_counts(uint32_t *ticks, uint32_t *sent, uint32_t *skipped);

#ifdef __cplusplus
}
#endif
