#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static volatile uint32_t s_total = 0, s_jam = 0, s_occ = 0, s_other = 0;
#define UNIQ_MAX 256
static uint32_t s_uniq[UNIQ_MAX];
static volatile int s_uniq_n = 0;

static uint32_t ssid_hash(const uint8_t *p, int len)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h ? h : 1;
}

static void uniq_add(uint32_t h)
{
    for (int i = 0; i < s_uniq_n; i++) {
        if (s_uniq[i] == h) return;
    }
    if (s_uniq_n < UNIQ_MAX) {
        s_uniq[s_uniq_n++] = h;
    }
}

static void promisc_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *p = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;
    if (type != WIFI_PKT_MGMT || len < 38) return;
    if ((p[0] & 0xF0) != 0x80) return;
    s_total++;
    int i = 36;
    while (i + 2 <= len) {
        uint8_t id = p[i];
        uint8_t l = p[i + 1];
        if (i + 2 + l > len) break;
        if (id == 0) {
            if (l > 0) uniq_add(ssid_hash(p + i + 2, l));
            if (l == 7 && memcmp(p + i + 2, "JAMAP13", 7) == 0) s_jam++;
            else if (l == 5 && memcmp(p + i + 2, "occ13", 5) == 0) s_occ++;
            else s_other++;
            break;
        }
        i += 2 + l;
    }
}

static void stats_task(void *arg)
{
    uint32_t lt = 0, lj = 0, lo = 0, lx = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t t = s_total, j = s_jam, o = s_occ, x = s_other;
        int u = s_uniq_n;
        s_uniq_n = 0;
        printf("bcn/s=%lu jam13=%lu occ=%lu other=%lu uniq=%d total=%lu jt=%lu\n",
               (unsigned long)(t - lt), (unsigned long)(j - lj),
               (unsigned long)(o - lo), (unsigned long)(x - lx), u,
               (unsigned long)t, (unsigned long)j);
        lt = t; lj = j; lo = o; lx = x;
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
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_country_code("CN", true));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t sta = {0};
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(13, WIFI_SECOND_CHAN_NONE));
    esp_wifi_set_ps(WIFI_PS_NONE);

    wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filt));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(promisc_cb));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    uint8_t ch = 0;
    wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&ch, &sec);
    printf("scan ch=%d promisc up\n", ch);

    xTaskCreate(stats_task, "stats", 3072, NULL, 3, NULL);
}
