#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "lwip/sockets.h"

static uint8_t own_addr_type;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t chr_val_handle = 0;
static volatile uint32_t s_sent = 0;
static volatile uint32_t s_err = 0;
static const char *dev_name = "C6-CONN";

static const ble_uuid16_t svc_uuid = BLE_UUID16_INIT(0xFFF0);
static const ble_uuid16_t chr_uuid = BLE_UUID16_INIT(0xFFF1);

static EventGroupHandle_t s_ev;
#define GOT_IP_BIT BIT0
static volatile bool s_flood = false;

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &chr_uuid.u,
                .access_cb = chr_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &chr_val_handle,
            },
            {0},
        },
    },
    {0},
};

static void start_advertising(void);

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            printf("connected handle=%d\n", conn_handle);
        } else {
            printf("connect failed status=%d\n", event->connect.status);
            start_advertising();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        printf("disconnected reason=%d\n", event->disconnect.reason);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        start_advertising();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return 0;
    default:
        return 0;
    }
}

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)dev_name;
    fields.name_len = strlen(dev_name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        printf("adv_fields rc=%d\n", rc);
        return;
    }

    struct ble_gap_adv_params p = {0};
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = 32;
    p.itvl_max = 32;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    printf("adv_start rc=%d\n", rc);
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        printf("id_infer rc=%d\n", rc);
        return;
    }
    start_advertising();
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void notify_task(void *param)
{
    uint8_t val[4];
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(15));
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE && chr_val_handle != 0) {
            uint32_t n = s_sent;
            memcpy(val, &n, sizeof val);
            struct os_mbuf *om = ble_hs_mbuf_from_flat(val, sizeof val);
            if (!om) {
                s_err++;
                continue;
            }
            int rc = ble_gatts_notify_custom(conn_handle, chr_val_handle, om);
            if (rc == 0) s_sent++; else s_err++;
        }
    }
}

static void stats_task(void *param)
{
    uint32_t last_s = 0, last_e = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t s = s_sent, e = s_err;
        wifi_ap_record_t ap = {0};
        int wch = -1, wrssi = 0;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            wch = ap.primary;
            wrssi = ap.rssi;
        }
        printf("sent=%lu err=%lu conn=%d flood=%d wch=%d rssi=%d\n",
               (unsigned long)(s - last_s), (unsigned long)(e - last_e),
               conn_handle != BLE_HS_CONN_HANDLE_NONE, s_flood, wch, wrssi);
        last_s = s;
        last_e = e;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t *)data;
        printf("wifi disconnected reason=%d\n", e->reason);
        esp_wifi_connect();
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        printf("got ip " IPSTR "\n", IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_ev, GOT_IP_BIT);
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
    dst.sin_addr.s_addr = inet_addr("192.168.4.1");

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
                printf("flood=%d sent=%lu err=%lu conn=%d\n", s_flood,
                       (unsigned long)s_sent, (unsigned long)s_err,
                       conn_handle != BLE_HS_CONN_HANDLE_NONE);
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

    s_ev = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_country_code("CN", true));

    wifi_config_t sta = {0};
    const char *ssid = "JAMAP13";
    memcpy(sta.sta.ssid, ssid, strlen(ssid));
    sta.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    uint8_t smac[6];
    esp_wifi_get_mac(WIFI_IF_STA, smac);
    printf("sta_mac=" MACSTR "\n", MAC2STR(smac));

    printf("wifi started, BLE starting\n");
    xTaskCreate(wifi_flood_task, "wflood", 4096, NULL, 5, NULL);
    xTaskCreate(cli_task, "cli", 3072, NULL, 4, NULL);

    nimble_port_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(gatt_svcs);
    printf("count_cfg rc=%d\n", rc);
    rc = ble_gatts_add_svcs(gatt_svcs);
    printf("add_svcs rc=%d chr_handle=%d\n", rc, chr_val_handle);

    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    xTaskCreate(notify_task, "notify", 3072, NULL, 4, NULL);
    xTaskCreate(stats_task, "stats", 3072, NULL, 3, NULL);
}
