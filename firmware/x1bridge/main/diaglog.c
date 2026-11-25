#include "diaglog.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define DL_RING_LINES 200
#define DL_LINE_MAX   160

static char            s_ring[DL_RING_LINES][DL_LINE_MAX];
static uint16_t        s_ring_head;      // 다음에 쓸 위치
static bool            s_ring_wrapped;
static SemaphoreHandle_t s_lock;
static dl_level_t      s_level[DL_CAT_COUNT];
static char            s_trace_id[8] = "-";
static uint32_t        s_dropped;
static uint32_t        s_written;

static const char *k_cat_name[DL_CAT_COUNT] = {
    "sys", "cfg", "ble.adv", "ble.link", "ble.gatt", "vsrc", "input",
};
static const char *k_lvl_name[] = { "ERROR", "WARN ", "INFO ", "TRACE" };

const char *dl_cat_name(dl_cat_t c)
{
    return (c >= 0 && c < DL_CAT_COUNT) ? k_cat_name[c] : "?";
}

const char *dl_level_name(dl_level_t l)
{
    return (l >= DL_LVL_ERROR && l <= DL_LVL_TRACE) ? k_lvl_name[l] : "?";
}

void dl_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    for (int i = 0; i < DL_CAT_COUNT; i++) {
        s_level[i] = DL_LVL_INFO;
    }
    s_ring_head = 0;
    s_ring_wrapped = false;
    s_dropped = 0;
    s_written = 0;
}

void dl_set_level(dl_cat_t c, dl_level_t l)
{
    if (c >= 0 && c < DL_CAT_COUNT) {
        s_level[c] = l;
    }
}

void dl_set_level_all(dl_level_t l)
{
    for (int i = 0; i < DL_CAT_COUNT; i++) {
        s_level[i] = l;
    }
}

dl_level_t dl_get_level(dl_cat_t c)
{
    return (c >= 0 && c < DL_CAT_COUNT) ? s_level[c] : DL_LVL_ERROR;
}

void dl_set_trace_id(const char *tid)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    if (tid && tid[0]) {
        strncpy(s_trace_id, tid, sizeof(s_trace_id) - 1);
        s_trace_id[sizeof(s_trace_id) - 1] = '\0';
    } else {
        strcpy(s_trace_id, "-");
    }
    if (s_lock) xSemaphoreGive(s_lock);
}

const char *dl_trace_id(void)
{
    return s_trace_id;
}

void dl_new_trace_id(void)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%04" PRIx32, esp_random() & 0xFFFF);
    dl_set_trace_id(buf);
}

uint32_t dl_dropped(void) { return s_dropped; }
uint32_t dl_written(void) { return s_written; }

// 공통 기록 경로. 콘솔과 링버퍼에 같은 줄을 남긴다.
static void dl_emit(dl_cat_t c, dl_level_t l, const char *body)
{
    uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
    char line[DL_LINE_MAX];
    snprintf(line, sizeof(line), "%8" PRIu32 " | %s | %-8s | %-4s | %s",
             ms, dl_level_name(l), dl_cat_name(c), s_trace_id, body);

    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    strncpy(s_ring[s_ring_head], line, DL_LINE_MAX - 1);
    s_ring[s_ring_head][DL_LINE_MAX - 1] = '\0';
    s_ring_head = (uint16_t)((s_ring_head + 1) % DL_RING_LINES);
    if (s_ring_head == 0) s_ring_wrapped = true;
    s_written++;
    if (s_lock) xSemaphoreGive(s_lock);

    printf("%s\n", line);
}

void dl_log(dl_cat_t c, dl_level_t l, const char *fmt, ...)
{
    if (l > dl_get_level(c)) {
        s_dropped++;
        return;
    }
    char body[DL_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    dl_emit(c, l, body);
}

// 오류는 레벨에 상관없이 항상 남긴다. domain/code/msg 3종 고정.
void dl_err(dl_cat_t c, const char *domain, int code, const char *fmt, ...)
{
    char msg[DL_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char body[DL_LINE_MAX];
    snprintf(body, sizeof(body), "domain=%s code=%d msg=%s",
             domain ? domain : "?", code, msg);
    dl_emit(c, DL_LVL_ERROR, body);
}

void dl_ring_clear(void)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    s_ring_head = 0;
    s_ring_wrapped = false;
    if (s_lock) xSemaphoreGive(s_lock);
}

void dl_dump(void)
{
    printf("\n----- DiagLog dump (written=%" PRIu32 " dropped=%" PRIu32 ") -----\n",
           s_written, s_dropped);
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    uint16_t start = s_ring_wrapped ? s_ring_head : 0;
    uint16_t count = s_ring_wrapped ? DL_RING_LINES : s_ring_head;
    for (uint16_t i = 0; i < count; i++) {
        printf("%s\n", s_ring[(start + i) % DL_RING_LINES]);
    }
    if (s_lock) xSemaphoreGive(s_lock);
    printf("----- end of dump -----\n\n");
}
