// diaglog.h — RD4tvOS 공통 진단 로거 (ESP32-C3 중계기용)
//
// 설계 원칙 (프로젝트 지침):
//   - 버그가 날 때마다 로그 코드를 고치지 않는다. 처음부터 포괄적으로 남긴다.
//   - 카테고리별 Logger, 레벨은 error/warn/info/trace.
//   - 모든 줄에 traceId(연결 세션 1건 = 1개)를 붙여 BLE·패턴·GATT 로그가 한 줄로 엮이게 한다.
//   - 오류는 반드시 domain / code / msg 3종을 함께 남긴다.
//   - 레벨은 런타임에 바꾼다. 상세 로그를 보려고 재빌드하지 않는다.
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DL_CAT_SYS = 0,    // 기동·종료·힙·리셋
    DL_CAT_CFG,        // 런타임 토글 변경 전후 값
    DL_CAT_BLE_ADV,    // 광고 시작·중지·완료
    DL_CAT_BLE_LINK,   // 연결·해제·구간(interval)·MTU
    DL_CAT_BLE_GATT,   // 구독·프로토콜모드 write·리포트 알림
    DL_CAT_VSRC,       // 가상 델타 패턴 생성기
    DL_CAT_INPUT,      // 실제 마우스 중계 (2단계에서 사용)
    DL_CAT_COUNT
} dl_cat_t;

typedef enum {
    DL_LVL_ERROR = 0,
    DL_LVL_WARN  = 1,
    DL_LVL_INFO  = 2,
    DL_LVL_TRACE = 3
} dl_level_t;

void        dl_init(void);
void        dl_set_trace_id(const char *tid);   // NULL 이면 "-"
const char *dl_trace_id(void);
void        dl_new_trace_id(void);              // 무작위 4자리 16진수 발급
void        dl_set_level(dl_cat_t c, dl_level_t l);
void        dl_set_level_all(dl_level_t l);
dl_level_t  dl_get_level(dl_cat_t c);
const char *dl_cat_name(dl_cat_t c);
const char *dl_level_name(dl_level_t l);

void dl_log(dl_cat_t c, dl_level_t l, const char *fmt, ...);
void dl_err(dl_cat_t c, const char *domain, int code, const char *fmt, ...);

void     dl_dump(void);        // 링버퍼 전체를 콘솔로
void     dl_ring_clear(void);
uint32_t dl_dropped(void);     // 레벨에 걸려 버려진 줄 수
uint32_t dl_written(void);

#define DL_E(cat, dom, code, ...) dl_err((cat), (dom), (code), __VA_ARGS__)
#define DL_W(cat, ...)            dl_log((cat), DL_LVL_WARN,  __VA_ARGS__)
#define DL_I(cat, ...)            dl_log((cat), DL_LVL_INFO,  __VA_ARGS__)
#define DL_T(cat, ...)            dl_log((cat), DL_LVL_TRACE, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
