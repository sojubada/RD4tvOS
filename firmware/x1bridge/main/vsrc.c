#include "vsrc.h"

#include <inttypes.h>
#include <math.h>

#include "diaglog.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "x1_profile.h"

#define VSRC_HZ         100     // 리포트 송신율
#define VSRC_RADIUS_PX  300.0f  // 원 반지름 (화면 픽셀 기준 추정치)
#define VSRC_PERIOD_S   4.0f    // 한 바퀴에 걸리는 시간
#define VSRC_SWEEP_PX   600.0f  // 좌우 왕복 폭
#define VSRC_STEP_PX    10      // STEP 모드 1회 이동량

static vsrc_mode_t s_mode = VSRC_CIRCLE;
static uint32_t    s_ticks, s_sent, s_skipped;

static const char *k_mode_name[VSRC_MODE_COUNT] = {
    "off", "circle", "hsweep", "step",
};

const char *vsrc_mode_name(vsrc_mode_t m)
{
    return (m >= 0 && m < VSRC_MODE_COUNT) ? k_mode_name[m] : "?";
}

void vsrc_set_mode(vsrc_mode_t m)
{
    if (m < 0 || m >= VSRC_MODE_COUNT) return;
    vsrc_mode_t old = s_mode;
    s_mode = m;
    DL_I(DL_CAT_CFG, "vsrc.mode %s -> %s", vsrc_mode_name(old),
         vsrc_mode_name(m));
}

vsrc_mode_t vsrc_get_mode(void) { return s_mode; }

void vsrc_cycle_mode(void)
{
    vsrc_set_mode((vsrc_mode_t)((s_mode + 1) % VSRC_MODE_COUNT));
}

void vsrc_get_counts(uint32_t *ticks, uint32_t *sent, uint32_t *skipped)
{
    if (ticks)   *ticks   = s_ticks;
    if (sent)    *sent    = s_sent;
    if (skipped) *skipped = s_skipped;
}

// 목표 위치를 먼저 계산하고 그 차이를 보낸다. 잔여 소수는 누적해서 다음 틱으로 넘긴다.
// 이렇게 하면 100Hz에서 반올림 오차가 쌓여 궤적이 흐르는 일이 없다.
static void emit_delta(float tgt_x, float tgt_y, float *pos_x, float *pos_y)
{
    float fdx = tgt_x - *pos_x;
    float fdy = tgt_y - *pos_y;
    int   dx  = (int)lroundf(fdx);
    int   dy  = (int)lroundf(fdy);

    if (dx == 0 && dy == 0) {
        return;   // 보낼 게 없으면 라디오를 쓰지 않는다
    }
    *pos_x += (float)dx;
    *pos_y += (float)dy;

    int rc = x1_send_xy(dx, dy);
    if (rc == 0) {
        s_sent++;
    } else {
        s_skipped++;
    }
}

static void vsrc_task(void *arg)
{
    const TickType_t period = pdMS_TO_TICKS(1000 / VSRC_HZ);
    float t = 0.0f;
    float pos_x = 0.0f, pos_y = 0.0f;
    uint32_t step_tick = 0;
    uint32_t last_report_ms = 0;

    DL_I(DL_CAT_VSRC, "task started hz=%d mode=%s", VSRC_HZ,
         vsrc_mode_name(s_mode));

    for (;;) {
        s_ticks++;
        bool ready = x1_ready_for_xy();

        if (!ready) {
            // 연결이 끊긴 사이 위상과 누적 위치를 초기화해 재연결 시 튀지 않게 한다.
            t = 0.0f;
            pos_x = pos_y = 0.0f;
            step_tick = 0;
        } else {
            switch (s_mode) {
            case VSRC_OFF:
                break;

            case VSRC_CIRCLE: {
                float w = 2.0f * (float)M_PI / VSRC_PERIOD_S;
                emit_delta(VSRC_RADIUS_PX * cosf(w * t) - VSRC_RADIUS_PX,
                           VSRC_RADIUS_PX * sinf(w * t), &pos_x, &pos_y);
                break;
            }

            case VSRC_HSWEEP: {
                // 삼각파. 왕복 1주기 = VSRC_PERIOD_S
                float ph = fmodf(t / VSRC_PERIOD_S, 1.0f);
                float x  = (ph < 0.5f) ? (ph * 2.0f) : (2.0f - ph * 2.0f);
                emit_delta(VSRC_SWEEP_PX * x, 0.0f, &pos_x, &pos_y);
                break;
            }

            case VSRC_STEP:
                if (++step_tick >= VSRC_HZ) {   // 1초마다
                    step_tick = 0;
                    int rc = x1_send_xy(VSRC_STEP_PX, 0);
                    if (rc == 0) {
                        s_sent++;
                        DL_I(DL_CAT_VSRC, "step sent dx=%d", VSRC_STEP_PX);
                    } else {
                        s_skipped++;
                    }
                }
                break;

            default:
                break;
            }
            t += 1.0f / (float)VSRC_HZ;
        }

        // 5초마다 한 줄 요약. 원인을 가를 숫자를 남긴다.
        uint32_t now_ms = s_ticks * (1000 / VSRC_HZ);
        if (now_ms - last_report_ms >= 5000) {
            last_report_ms = now_ms;
            x1_status_t st;
            x1_get_status(&st);
            DL_I(DL_CAT_VSRC,
                 "tick=%" PRIu32 " mode=%s ready=%d sent=%" PRIu32
                 " skip=%" PRIu32 " nxy=%" PRIu32 " nerr=%" PRIu32
                 " clamp=%" PRIu32 " itvl=%u boot=%d",
                 s_ticks, vsrc_mode_name(s_mode), (int)ready, s_sent, s_skipped,
                 st.notify_xy, st.notify_err, st.clamped, st.conn_itvl_125us,
                 (int)st.boot_mode);
        }

        vTaskDelay(period);
    }
}

void vsrc_start(void)
{
    BaseType_t rc = xTaskCreate(vsrc_task, "vsrc", 4096, NULL, 5, NULL);
    if (rc != pdPASS) {
        DL_E(DL_CAT_VSRC, "freertos", (int)rc, "xTaskCreate failed");
    }
}
