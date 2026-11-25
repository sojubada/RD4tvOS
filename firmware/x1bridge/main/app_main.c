// app_main.c — RD4tvOS X1 Bridge (ESP32-C3)
//
// 1단계 목표: 애플TV 앱 코드 없이 "ESP32 → 애플TV" 경로를 검증한다.
//   보드를 꽂고 기존 Moonlight TV에서 X1 마우스를 켜면 커서가 원을 그려야 한다.
//   그러면 BLE 광고·연결·GATT·0x2908 디스크립터·12비트 좌표 패킹·tvOS 수신이 전부 통과다.
//   움직이지 않으면 어디서 막혔는지 DiagLog가 지목한다.
//
// 2단계(별도 커밋): 실제 Microsoft Surface/Modern Mobile Mouse(1679/1679C)를 central로
//   붙여 HOGP 리포트를 받아 이 경로로 중계한다. 그때 본딩·IRK·Report Map 파싱이 추가된다.
//
// 상태 LED (보드 내장, GPIO8 / active-low):
//   0.5초 점멸  = 광고 중 (아무도 안 붙었다)
//   0.1초 점멸  = 연결됐지만 구독 안 됨 (앱이 notify를 켜지 않았다)
//   계속 켜짐   = 연결 + 구독 완료 (리포트를 보내는 중)
//   꺼짐        = 호스트 미동기 / 오류
//
// USB 시리얼 단일문자 명령 (재빌드 없이 조절 — 릴리즈 최소화):
//   h 도움말   s 상태·통계   d 로그 덤프   c 로그 비우기
//   p 패턴 전환(off/circle/hsweep/step)
//   + / -  전체 로그 레벨 올리기 / 내리기
//   g 'ble.gatt'만 TRACE 토글 (리포트 한 줄 한 줄 확인용)
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>

#include "diaglog.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "vsrc.h"
#include "x1_profile.h"

#define FW_VERSION  "x1bridge 0.1.0"
#define LED_GPIO    8        // ESP32-C3 SuperMini 내장 LED. 보드가 다르면 여기만 바꾼다.
#define LED_ACTIVE_LOW 1

static void led_set(bool on)
{
#if LED_ACTIVE_LOW
    gpio_set_level(LED_GPIO, on ? 0 : 1);
#else
    gpio_set_level(LED_GPIO, on ? 1 : 0);
#endif
}

static void led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    led_set(false);
}

static void led_update(void)
{
    static uint32_t phase;
    x1_status_t st;
    x1_get_status(&st);
    phase++;

    if (st.connected && st.xy_subscribed) {
        led_set(true);
    } else if (st.connected) {
        led_set((phase / 2) % 2 == 0);    // 100ms 주기
    } else {
        led_set((phase / 10) % 2 == 0);   // 500ms 주기
    }
}

static void print_help(void)
{
    printf("\n--- " FW_VERSION " ---\n"
           "h  이 도움말\n"
           "s  상태와 통계\n"
           "d  DiagLog 링버퍼 덤프\n"
           "c  링버퍼 비우기\n"
           "p  가상 패턴 전환 (off/circle/hsweep/step)\n"
           "+  로그 레벨 올리기   -  내리기\n"
           "g  ble.gatt 만 TRACE 토글\n"
           "R  재부팅\n\n");
}

static void print_status(void)
{
    x1_status_t st;
    x1_get_status(&st);
    uint32_t ticks, sent, skipped;
    vsrc_get_counts(&ticks, &sent, &skipped);

    printf("\n--- status ---\n");
    printf("fw            : %s\n", FW_VERSION);
    printf("uptime        : %" PRIu32 " s\n",
           (uint32_t)(esp_timer_get_time() / 1000000));
    printf("free heap     : %" PRIu32 " B\n", (uint32_t)esp_get_free_heap_size());
    printf("trace id      : %s\n", dl_trace_id());
    printf("connected     : %d (conn_count=%" PRIu32 ")\n", (int)st.connected,
           st.conn_count);
    printf("xy subscribed : %d\n", (int)st.xy_subscribed);
    printf("btn subscribed: %d\n", (int)st.btn_subscribed);
    printf("boot mode     : %d  (central이 0x2A4E에 0x00을 썼는가)\n",
           (int)st.boot_mode);
    printf("conn interval : %u (= %u.%02u ms)\n", st.conn_itvl_125us,
           st.conn_itvl_125us * 125 / 100, (st.conn_itvl_125us * 125) % 100);
    printf("mtu           : %u\n", st.mtu);
    printf("notify xy/btn : %" PRIu32 " / %" PRIu32 "\n", st.notify_xy,
           st.notify_btn);
    printf("notify err    : %" PRIu32 "\n", st.notify_err);
    printf("clamped       : %" PRIu32 "\n", st.clamped);
    printf("vsrc          : mode=%s tick=%" PRIu32 " sent=%" PRIu32
           " skipped=%" PRIu32 "\n",
           vsrc_mode_name(vsrc_get_mode()), ticks, sent, skipped);
    printf("log written   : %" PRIu32 " (dropped=%" PRIu32 ")\n", dl_written(),
           dl_dropped());
    printf("log levels    : ");
    for (int i = 0; i < DL_CAT_COUNT; i++) {
        printf("%s=%s ", dl_cat_name((dl_cat_t)i),
               dl_level_name(dl_get_level((dl_cat_t)i)));
    }
    printf("\n--------------\n\n");
}

static void handle_cmd(int ch)
{
    static dl_level_t all_level = DL_LVL_INFO;
    static bool gatt_trace = false;

    switch (ch) {
    case 'h': case '?':
        print_help();
        break;
    case 's':
        print_status();
        break;
    case 'd':
        dl_dump();
        break;
    case 'c':
        dl_ring_clear();
        printf("ring cleared\n");
        break;
    case 'p':
        vsrc_cycle_mode();
        printf("vsrc mode = %s\n", vsrc_mode_name(vsrc_get_mode()));
        break;
    case '+':
        if (all_level < DL_LVL_TRACE) all_level++;
        dl_set_level_all(all_level);
        gatt_trace = (all_level == DL_LVL_TRACE);
        DL_I(DL_CAT_CFG, "log.level all -> %s", dl_level_name(all_level));
        break;
    case '-':
        if (all_level > DL_LVL_ERROR) all_level--;
        dl_set_level_all(all_level);
        gatt_trace = false;
        DL_I(DL_CAT_CFG, "log.level all -> %s", dl_level_name(all_level));
        break;
    case 'g':
        gatt_trace = !gatt_trace;
        dl_set_level(DL_CAT_BLE_GATT, gatt_trace ? DL_LVL_TRACE : all_level);
        DL_I(DL_CAT_CFG, "log.level ble.gatt -> %s",
             dl_level_name(dl_get_level(DL_CAT_BLE_GATT)));
        break;
    case 'R':
        DL_W(DL_CAT_SYS, "reboot requested from console");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
        break;
    default:
        break;
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    dl_init();
    DL_I(DL_CAT_SYS, "boot %s heap=%" PRIu32 " nvs=%d", FW_VERSION,
         (uint32_t)esp_get_free_heap_size(), (int)err);
    if (err != ESP_OK) {
        DL_E(DL_CAT_SYS, "esp", (int)err, "nvs_flash_init failed");
    }

    led_init();
    x1_init();
    vsrc_start();
    print_help();

    // 콘솔 입력을 비차단으로 읽는다. USB가 전원 전용 포트에 꽂혀 있어도 그냥 통과한다.
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    setvbuf(stdin, NULL, _IONBF, 0);

    uint32_t beat = 0;
    for (;;) {
        int ch = fgetc(stdin);
        while (ch != EOF) {
            handle_cmd(ch);
            ch = fgetc(stdin);
        }

        led_update();

        // 30초 하트비트. 강제종료·행업의 마지막 지점을 남긴다.
        if (++beat % 600 == 0) {
            x1_status_t st;
            x1_get_status(&st);
            DL_I(DL_CAT_SYS,
                 "heartbeat up=%" PRIu32 "s heap=%" PRIu32 " conn=%d sub=%d",
                 (uint32_t)(esp_timer_get_time() / 1000000),
                 (uint32_t)esp_get_free_heap_size(), (int)st.connected,
                 (int)st.xy_subscribed);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
