#include "x1_profile.h"

#include <inttypes.h>
#include <string.h>

#include "diaglog.h"
#include "esp_err.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// 2B080000-BDB5-F6EB-24AE-9D6AB282AB63 을 리틀엔디언 바이트열로 (NimBLE 규약)
static const ble_uuid128_t k_svc_uuid = BLE_UUID128_INIT(
    0x63, 0xab, 0x82, 0xb2, 0x6a, 0x9d, 0xae, 0x24,
    0xeb, 0xf6, 0xb5, 0xbd, 0x00, 0x00, 0x08, 0x2b);

static const ble_uuid16_t k_uuid_proto_mode = BLE_UUID16_INIT(0x2A4E);
static const ble_uuid16_t k_uuid_report     = BLE_UUID16_INIT(0x2A4D);
static const ble_uuid16_t k_uuid_report_ref = BLE_UUID16_INIT(0x2908);

// Report Reference 값. 빅엔디언으로 읽으면 0x0201 / 0x0101 이 된다.
// 바이트0 = 리포트 ID, 바이트1 = 리포트 타입(1 = Input).
static const uint8_t k_rr_xy[2]  = { 0x02, 0x01 };
static const uint8_t k_rr_btn[2] = { 0x01, 0x01 };

static uint16_t s_h_proto;
static uint16_t s_h_xy;
static uint16_t s_h_btn;

static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint8_t  s_own_addr_type;
static uint8_t  s_proto_mode = 0x01;   // 1 = Report Protocol (기본값)
static uint8_t  s_last_xy[3];
static uint8_t  s_last_btn[3];
static x1_status_t s_st;

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg);
static void adv_start(void);

static const struct ble_gatt_dsc_def k_dsc_xy[] = {
    {
        .uuid      = &k_uuid_report_ref.u,
        .att_flags = BLE_ATT_F_READ,
        .access_cb = gatt_access,
        .arg       = (void *)k_rr_xy,
    },
    { 0 }
};

static const struct ble_gatt_dsc_def k_dsc_btn[] = {
    {
        .uuid      = &k_uuid_report_ref.u,
        .att_flags = BLE_ATT_F_READ,
        .access_cb = gatt_access,
        .arg       = (void *)k_rr_btn,
    },
    { 0 }
};

static const struct ble_gatt_chr_def k_chrs[] = {
    {
        .uuid       = &k_uuid_proto_mode.u,
        .access_cb  = gatt_access,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                      BLE_GATT_CHR_F_WRITE_NO_RSP,
        .val_handle = &s_h_proto,
    },
    {
        .uuid        = &k_uuid_report.u,
        .access_cb   = gatt_access,
        .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .descriptors = (struct ble_gatt_dsc_def *)k_dsc_xy,
        .val_handle  = &s_h_xy,
    },
    {
        .uuid        = &k_uuid_report.u,
        .access_cb   = gatt_access,
        .flags       = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .descriptors = (struct ble_gatt_dsc_def *)k_dsc_btn,
        .val_handle  = &s_h_btn,
    },
    { 0 }
};

static const struct ble_gatt_svc_def k_svcs[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &k_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def *)k_chrs,
    },
    { 0 }
};

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_DSC: {
        const uint8_t *rr = (const uint8_t *)arg;
        DL_T(DL_CAT_BLE_GATT, "read report-ref handle=%u val=%02x%02x",
             attr_handle, rr[0], rr[1]);
        return os_mbuf_append(ctxt->om, rr, 2) == 0 ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    case BLE_GATT_ACCESS_OP_READ_CHR: {
        const uint8_t *src;
        uint16_t len;
        if (attr_handle == s_h_proto) {
            src = &s_proto_mode;
            len = 1;
        } else if (attr_handle == s_h_xy) {
            src = s_last_xy;
            len = sizeof(s_last_xy);
        } else if (attr_handle == s_h_btn) {
            src = s_last_btn;
            len = sizeof(s_last_btn);
        } else {
            DL_E(DL_CAT_BLE_GATT, "gatt", BLE_ATT_ERR_UNLIKELY,
                 "read on unknown handle=%u", attr_handle);
            return BLE_ATT_ERR_UNLIKELY;
        }
        DL_T(DL_CAT_BLE_GATT, "read chr handle=%u len=%u", attr_handle, len);
        return os_mbuf_append(ctxt->om, src, len) == 0 ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
        if (attr_handle != s_h_proto) {
            DL_E(DL_CAT_BLE_GATT, "gatt", BLE_ATT_ERR_WRITE_NOT_PERMITTED,
                 "write on non-writable handle=%u", attr_handle);
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        }
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        uint8_t  v   = 0xFF;
        if (len < 1) {
            DL_E(DL_CAT_BLE_GATT, "gatt", BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN,
                 "protocol-mode write len=%u", len);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        if (ble_hs_mbuf_to_flat(ctxt->om, &v, 1, NULL) != 0) {
            DL_E(DL_CAT_BLE_GATT, "gatt", BLE_ATT_ERR_UNLIKELY,
                 "protocol-mode mbuf_to_flat failed");
            return BLE_ATT_ERR_UNLIKELY;
        }
        s_proto_mode   = v;
        s_st.boot_mode = (v == 0x00);
        // 핵심 이벤트. 이 줄이 안 보이면 central이 직결 모드로 전환하지 않은 것이다.
        DL_I(DL_CAT_BLE_GATT, "protocol-mode write val=0x%02x boot_mode=%d len=%u",
             v, (int)s_st.boot_mode, len);
        return 0;
    }

    default:
        DL_W(DL_CAT_BLE_GATT, "unhandled gatt op=%d handle=%u", ctxt->op,
             attr_handle);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static void log_conn_params(uint16_t conn_handle, const char *when)
{
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn_handle, &desc) != 0) {
        return;
    }
    s_st.conn_itvl_125us = desc.conn_itvl;
    // conn_itvl 단위는 1.25ms. 애플 플랫폼의 하한은 15ms(=12)이므로 그 아래로는 내려가지 않는다.
    DL_I(DL_CAT_BLE_LINK,
         "%s itvl=%u(%u.%02ums) latency=%u timeout=%ums enc=%d authen=%d",
         when, desc.conn_itvl, desc.conn_itvl * 125 / 100,
         (desc.conn_itvl * 125) % 100, desc.conn_latency,
         desc.supervision_timeout * 10, desc.sec_state.encrypted,
         desc.sec_state.authenticated);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn            = event->connect.conn_handle;
            s_st.connected    = true;
            s_st.conn_count++;
            s_st.xy_subscribed  = false;
            s_st.btn_subscribed = false;
            s_st.boot_mode      = false;
            dl_new_trace_id();   // 연결 1건 = traceId 1개
            DL_I(DL_CAT_BLE_LINK, "connected handle=%u count=%" PRIu32,
                 s_conn, s_st.conn_count);
            log_conn_params(s_conn, "connected");

            // 포인터 지연을 줄이기 위해 짧은 구간을 요청한다. 애플 플랫폼은 15ms가 하한이고
            // 요청이 거절될 수 있으므로, 실제로 협상된 값을 CONN_UPDATE에서 다시 찍는다.
            struct ble_gap_upd_params up = {
                .itvl_min            = 12,   // 15.00 ms
                .itvl_max            = 24,   // 30.00 ms
                .latency             = 0,
                .supervision_timeout = 200,  // 2000 ms
                .min_ce_len          = 0,
                .max_ce_len          = 0,
            };
            int rc = ble_gap_update_params(s_conn, &up);
            if (rc != 0) {
                DL_E(DL_CAT_BLE_LINK, "nimble", rc, "update_params request failed");
            } else {
                DL_I(DL_CAT_BLE_LINK, "update_params requested min=15ms max=30ms");
            }
        } else {
            DL_E(DL_CAT_BLE_LINK, "nimble", event->connect.status,
                 "connect failed");
            adv_start();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        DL_I(DL_CAT_BLE_LINK, "disconnected reason=%d notify_xy=%" PRIu32
             " notify_btn=%" PRIu32 " notify_err=%" PRIu32 " clamped=%" PRIu32,
             event->disconnect.reason, s_st.notify_xy, s_st.notify_btn,
             s_st.notify_err, s_st.clamped);
        s_conn              = BLE_HS_CONN_HANDLE_NONE;
        s_st.connected      = false;
        s_st.xy_subscribed  = false;
        s_st.btn_subscribed = false;
        s_st.boot_mode      = false;
        s_st.conn_itvl_125us = 0;
        s_st.mtu            = 0;
        dl_set_trace_id(NULL);
        adv_start();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        if (event->conn_update.status != 0) {
            DL_E(DL_CAT_BLE_LINK, "nimble", event->conn_update.status,
                 "conn update failed");
        }
        log_conn_params(event->conn_update.conn_handle, "conn-update");
        return 0;

    case BLE_GAP_EVENT_MTU:
        s_st.mtu = event->mtu.value;
        DL_I(DL_CAT_BLE_LINK, "mtu=%u", event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE: {
        uint16_t h = event->subscribe.attr_handle;
        bool on = event->subscribe.cur_notify;
        if (h == s_h_xy) {
            s_st.xy_subscribed = on;
            DL_I(DL_CAT_BLE_GATT, "subscribe XY notify=%d", (int)on);
        } else if (h == s_h_btn) {
            s_st.btn_subscribed = on;
            DL_I(DL_CAT_BLE_GATT, "subscribe BTN notify=%d", (int)on);
        } else {
            DL_T(DL_CAT_BLE_GATT, "subscribe other handle=%u notify=%d", h,
                 (int)on);
        }
        return 0;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
        DL_W(DL_CAT_BLE_ADV, "adv complete reason=%d, restarting",
             event->adv_complete.reason);
        adv_start();
        return 0;

    default:
        DL_T(DL_CAT_BLE_LINK, "gap event type=%d", event->type);
        return 0;
    }
}

static void adv_start(void)
{
    struct ble_hs_adv_fields f = { 0 };
    // 0x1812 는 절대 넣지 않는다. 커스텀 서비스 UUID 하나만 광고한다.
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = (ble_uuid128_t *)&k_svc_uuid;
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&f);
    if (rc != 0) {
        DL_E(DL_CAT_BLE_ADV, "nimble", rc, "adv_set_fields failed");
        return;
    }

    // 128비트 UUID가 광고 패킷의 18바이트를 쓰므로 이름은 스캔 응답으로 보낸다.
    const char *name = ble_svc_gap_device_name();
    struct ble_hs_adv_fields rsp = { 0 };
    rsp.name = (uint8_t *)name;
    rsp.name_len = (uint8_t)strlen(name);
    rsp.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        DL_E(DL_CAT_BLE_ADV, "nimble", rc, "adv_rsp_set_fields failed");
        return;
    }

    struct ble_gap_adv_params advp = { 0 };
    advp.conn_mode = BLE_GAP_CONN_MODE_UND;
    advp.disc_mode = BLE_GAP_DISC_MODE_GEN;
    advp.itvl_min  = 48;   // 30 ms
    advp.itvl_max  = 96;   // 60 ms

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &advp,
                           gap_event, NULL);
    if (rc != 0) {
        DL_E(DL_CAT_BLE_ADV, "nimble", rc, "adv_start failed");
        return;
    }
    DL_I(DL_CAT_BLE_ADV, "advertising name='%s' svc=2B080000-...AB63", name);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        DL_E(DL_CAT_SYS, "nimble", rc, "ensure_addr failed");
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        DL_E(DL_CAT_SYS, "nimble", rc, "id_infer_auto failed");
        return;
    }
    uint8_t addr[6] = { 0 };
    ble_hs_id_copy_addr(s_own_addr_type, addr, NULL);
    DL_I(DL_CAT_SYS, "host synced addr_type=%u addr=%02x:%02x:%02x:%02x:%02x:%02x",
         s_own_addr_type, addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    adv_start();
}

static void on_reset(int reason)
{
    DL_E(DL_CAT_SYS, "nimble", reason, "host reset");
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void x1_init(void)
{
    memset(&s_st, 0, sizeof(s_st));
    memset(s_last_xy, 0, sizeof(s_last_xy));
    memset(s_last_btn, 0, sizeof(s_last_btn));

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        DL_E(DL_CAT_SYS, "esp", (int)err, "nimble_port_init failed");
        return;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    // 본딩·암호화를 요구하지 않는다. tvOS 설정에서 페어링하지 않고 앱이 바로 붙게 하려면
    // X1 특성이 평문으로 열려 있어야 한다.
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_svc_gap_device_name_set(X1_DEV_NAME);
    if (rc != 0) {
        DL_E(DL_CAT_SYS, "nimble", rc, "device_name_set failed");
    }

    rc = ble_gatts_count_cfg(k_svcs);
    if (rc != 0) {
        DL_E(DL_CAT_SYS, "nimble", rc, "gatts_count_cfg failed");
        return;
    }
    rc = ble_gatts_add_svcs(k_svcs);
    if (rc != 0) {
        DL_E(DL_CAT_SYS, "nimble", rc, "gatts_add_svcs failed");
        return;
    }

    nimble_port_freertos_init(host_task);
    DL_I(DL_CAT_SYS, "x1 profile registered (proto/xy/btn + report-ref dsc)");
}

void x1_get_status(x1_status_t *out)
{
    if (out) *out = s_st;
}

bool x1_ready_for_xy(void)
{
    return s_st.connected && s_st.xy_subscribed;
}

static int clamp_delta(int v, bool *did_clamp)
{
    if (v < X1_DELTA_MIN) { *did_clamp = true; return X1_DELTA_MIN; }
    if (v > X1_DELTA_MAX) { *did_clamp = true; return X1_DELTA_MAX; }
    return v;
}

int x1_send_xy(int dx, int dy)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_st.xy_subscribed) {
        return -1;
    }

    bool clamped = false;
    int cdx = clamp_delta(dx, &clamped);
    int cdy = clamp_delta(dy, &clamped);
    if (clamped) {
        s_st.clamped++;
        // 포화가 실제로 문제인지 숫자로 판정하기 위한 줄.
        DL_W(DL_CAT_VSRC, "delta clamped in=(%d,%d) out=(%d,%d) total=%" PRIu32,
             dx, dy, cdx, cdy, s_st.clamped);
    }

    // XY 리포트 3바이트, 12비트 패킹
    //   b0 = dx[7:0]
    //   b1 = dx[11:8] (하위 니블) | dy[3:0] (상위 니블)
    //   b2 = dy[11:4]
    uint16_t ux = (uint16_t)cdx & 0x0FFF;
    uint16_t uy = (uint16_t)cdy & 0x0FFF;
    uint8_t b[3];
    b[0] = (uint8_t)(ux & 0xFF);
    b[1] = (uint8_t)(((ux >> 8) & 0x0F) | ((uy & 0x0F) << 4));
    b[2] = (uint8_t)((uy >> 4) & 0xFF);
    memcpy(s_last_xy, b, sizeof(b));

    struct os_mbuf *om = ble_hs_mbuf_from_flat(b, sizeof(b));
    if (!om) {
        s_st.notify_err++;
        DL_E(DL_CAT_BLE_GATT, "nimble", -2, "mbuf alloc failed (xy)");
        return -2;
    }
    int rc = ble_gatts_notify_custom(s_conn, s_h_xy, om);
    if (rc != 0) {
        s_st.notify_err++;
        DL_E(DL_CAT_BLE_GATT, "nimble", rc, "notify xy failed");
        return rc;
    }
    s_st.notify_xy++;
    DL_T(DL_CAT_BLE_GATT, "notify xy d=(%d,%d) raw=%02x%02x%02x n=%" PRIu32,
         cdx, cdy, b[0], b[1], b[2], s_st.notify_xy);
    return 0;
}

int x1_send_btn_wheel(uint8_t buttons, int wheel)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_st.btn_subscribed) {
        return -1;
    }
    if (wheel > 127)  wheel = 127;
    if (wheel < -127) wheel = -127;

    // 3바이트형(구 펌웨어 레이아웃): b0 = 버튼 비트마스크, b1 = 휠(Int8), b2 = 예비.
    // 5바이트형이 필요하면 b1..b3에 XY를 싣고 b4를 휠로 쓴다.
    uint8_t b[3];
    b[0] = buttons & 0x07;
    b[1] = (uint8_t)(int8_t)wheel;
    b[2] = 0;
    memcpy(s_last_btn, b, sizeof(b));

    struct os_mbuf *om = ble_hs_mbuf_from_flat(b, sizeof(b));
    if (!om) {
        s_st.notify_err++;
        DL_E(DL_CAT_BLE_GATT, "nimble", -2, "mbuf alloc failed (btn)");
        return -2;
    }
    int rc = ble_gatts_notify_custom(s_conn, s_h_btn, om);
    if (rc != 0) {
        s_st.notify_err++;
        DL_E(DL_CAT_BLE_GATT, "nimble", rc, "notify btn failed");
        return rc;
    }
    s_st.notify_btn++;
    DL_I(DL_CAT_BLE_GATT, "notify btn mask=0x%02x wheel=%d n=%" PRIu32,
         b[0], wheel, s_st.notify_btn);
    return 0;
}
