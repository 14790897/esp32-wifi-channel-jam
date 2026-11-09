#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_hci.h"
#include "host/util/util.h"
#include "host/ble_gatt.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "lwip/sockets.h"

static uint8_t own_addr_type;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static volatile uint32_t s_recv = 0;
static const char *target_name = "C6-CONN";
static const ble_uuid16_t svc_uuid = BLE_UUID16_INIT(0xFFF0);
static const ble_uuid16_t chr_uuid = BLE_UUID16_INIT(0xFFF1);
static volatile bool s_flood = false;

static void start_scan(void);
static int gap_event(struct ble_gap_event *event, void *arg);

static bool adv_has_name(const uint8_t *data, uint8_t len)
{
    size_t nlen = strlen(target_name);
    uint8_t i = 0;
    while (i + 1 < len) {
        uint8_t ad_len = data[i];
        if (ad_len == 0 || i + 1 + ad_len > len) break;
        uint8_t type = data[i + 1];
        if ((type == 0x08 || type == 0x09) && ad_len >= 2) {
            uint8_t l = ad_len - 1;
            if (l == nlen && memcmp(&data[i + 2], target_name, nlen) == 0) {
                return true;
            }
        }
        i += ad_len + 1;
    }
    return false;
}

static int chr_cb(uint16_t conn, const struct ble_gatt_error *err,
                  const struct ble_gatt_chr *chr, void *arg)
{
    if (err->status == 0 && chr) {
        if (chr->uuid.u.type == BLE_UUID_TYPE_16 &&
            chr->uuid.u16.value == chr_uuid.value) {
            uint8_t val[2] = {1, 0};
            printf("subscribe val_handle=%d\n", chr->val_handle);
            ble_gattc_write_flat(conn, chr->val_handle + 1, val, sizeof val, NULL, NULL);
        }
    }
    return 0;
}

static int svc_cb(uint16_t conn, const struct ble_gatt_error *err,
                  const struct ble_gatt_svc *svc, void *arg)
{
    if (err->status == 0 && svc) {
        printf("svc handles %d-%d\n", svc->start_handle, svc->end_handle);
        ble_gattc_disc_all_chrs(conn, svc->start_handle, svc->end_handle, chr_cb, NULL);
    }
    return 0;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        if (adv_has_name(event->disc.data, event->disc.length_data)) {
            ble_gap_disc_cancel();
            struct ble_gap_conn_params cp = {0};
            cp.scan_itvl = 16;
            cp.scan_window = 16;
            cp.itvl_min = 12;
            cp.itvl_max = 12;
            cp.latency = 0;
            cp.supervision_timeout = 400;
            int rc = ble_gap_connect(own_addr_type, &event->disc.addr, 30000,
                                     &cp, gap_event, NULL);
            printf("connect rc=%d\n", rc);
        }
        return 0;
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            printf("connected handle=%d\n", conn_handle);
            uint8_t chan_map[5] = {0x03, 0x00, 0x00, 0x00, 0x00};
            int mrc = ble_hs_hci_set_chan_class(chan_map);
            uint8_t read_map[5] = {0};
            int rrc = ble_hs_hci_read_chan_map(conn_handle, read_map);
            printf("chan_class rc=%d read=%d map=%02x%02x%02x%02x%02x\n", mrc, rrc,
                   read_map[4], read_map[3], read_map[2], read_map[1], read_map[0]);
            ble_gattc_disc_svc_by_uuid(conn_handle, &svc_uuid.u, svc_cb, NULL);
        } else {
            printf("connect failed status=%d\n", event->connect.status);
            start_scan();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        printf("disconnected reason=%d\n", event->disconnect.reason);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        start_scan();
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:
        s_recv++;
        return 0;
    default:
        return 0;
    }
}

static void start_scan(void)
{
    struct ble_gap_disc_params p = {0};
    p.itvl = 16;
    p.window = 16;
    p.passive = 1;
    p.filter_duplicates = 0;
    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &p, gap_event, NULL);
    printf("scan rc=%d\n", rc);
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        printf("id_infer rc=%d\n", rc);
        return;
    }
    start_scan();
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void stats_task(void *param)
{
    uint32_t last = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t c = s_recv;
        uint8_t map[5] = {0};
        int mr = -1;
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            mr = ble_hs_hci_read_chan_map(conn_handle, map);
        }
        printf("recv=%lu conn=%d flood=%d maprc=%d map=%02x%02x%02x%02x%02x\n",
               (unsigned long)(c - last), conn_handle != BLE_HS_CONN_HANDLE_NONE,
               s_flood, mr, map[4], map[3], map[2], map[1], map[0]);
        last = c;
    }
}

static void udp_server_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3333);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(sock, (struct sockaddr *)&addr, sizeof addr);
    static char buf[1500];
    while (1) {
        recvfrom(sock, buf, sizeof buf, 0, NULL, NULL);
    }
}

static void wifi_flood_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    int b = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &b, sizeof b);
    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(3333);
    dst.sin_addr.s_addr = inet_addr("192.168.4.255");

    static char buf[1400];
    memset(buf, 0xAA, sizeof buf);

    uint32_t sent = 0, last = 0;
    TickType_t last_tick = xTaskGetTickCount();
    while (1) {
        if (s_flood) {
            int r = sendto(sock, buf, sizeof buf, 0, (struct sockaddr *)&dst, sizeof dst);
            if (r > 0) sent++; else vTaskDelay(1);
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (xTaskGetTickCount() - last_tick >= pdMS_TO_TICKS(1000)) {
            printf("udp=%lu\n", (unsigned long)(sent - last));
            last = sent;
            last_tick = xTaskGetTickCount();
        }
    }
}

static void cli_task(void *arg)
{
    char line[32];
    while (1) {
        if (fgets(line, sizeof line, stdin)) {
            if (strncmp(line, "start", 5) == 0) {
                s_flood = true;
                printf("wifi flood on\n");
            } else if (strncmp(line, "stop", 4) == 0) {
                s_flood = false;
                printf("wifi flood off\n");
            } else if (strncmp(line, "status", 6) == 0) {
                printf("flood=%d recv=%lu conn=%d\n", s_flood,
                       (unsigned long)s_recv, conn_handle != BLE_HS_CONN_HANDLE_NONE);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_country_code("CN", true));

    wifi_config_t ap = {0};
    const char *ssid = "JAMAP13";
    memcpy(ap.ap.ssid, ssid, strlen(ssid));
    ap.ap.ssid_len = strlen(ssid);
    ap.ap.channel = 13;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.max_connection = 4;
    ap.ap.beacon_interval = 100;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    printf("AP JAMAP13 ch13 up\n");

    xTaskCreate(udp_server_task, "udps", 4096, NULL, 4, NULL);
    xTaskCreate(wifi_flood_task, "wflood", 4096, NULL, 5, NULL);
    xTaskCreate(cli_task, "cli", 3072, NULL, 4, NULL);

    nimble_port_init();
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    xTaskCreate(stats_task, "stats", 3072, NULL, 3, NULL);
}
