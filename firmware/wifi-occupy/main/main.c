#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "nvs_flash.h"

#ifndef OCC_CHANNEL
#define OCC_CHANNEL 13
#endif
#define FRAME_LEN 1500

static volatile bool s_run = false;
static volatile bool s_nav = true;
static volatile int s_mode = 0;
static volatile uint32_t s_ok = 0, s_err = 0, s_fps = 0;

static size_t build_bcn_pkt(uint8_t *pkt)
{
    uint8_t *p = pkt;
    *p++ = 0x80; *p++ = 0x00;
    *p++ = s_nav ? 0xFF : 0x00;
    *p++ = s_nav ? 0xFF : 0x00;
    memset(p, 0xff, 6); p += 6;
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    memcpy(p, mac, 6); p += 6;
    memcpy(p, mac, 6); p += 6;

    static uint16_t seq = 0;
    seq = (seq + 1) & 0x0FFF;
    *p++ = (uint8_t)((seq & 0x000F) << 4);
    *p++ = (uint8_t)(seq >> 4);

    static uint64_t tsf = 0;
    tsf += 100000;
    memcpy(p, &tsf, 8); p += 8;

    *p++ = 0x64; *p++ = 0x00;
    *p++ = 0x01; *p++ = 0x00;
    *p++ = 0x00; *p++ = 0x01; *p++ = 'o';

    static const uint8_t rates[] = {0x01, 0x04, 0x82, 0x84, 0x8B, 0x96};
    memcpy(p, rates, sizeof rates); p += sizeof rates;

    *p++ = 0x03; *p++ = 0x01; *p++ = OCC_CHANNEL;

    static const uint8_t tim[] = {0x05, 0x04, 0x00, 0x01, 0x00, 0x00};
    memcpy(p, tim, sizeof tim); p += sizeof tim;

    size_t cur = (size_t)(p - pkt);
    while (cur < FRAME_LEN) {
        size_t left = FRAME_LEN - cur;
        if (left < 2) break;
        size_t n = left - 2;
        if (n > 255) n = 255;
        *p++ = 0xDD; *p++ = (uint8_t)n;
        memset(p, 0xAA, n); p += n;
        cur = (size_t)(p - pkt);
    }
    return (size_t)(p - pkt);
}

static size_t build_data_pkt(uint8_t *pkt)
{
    uint8_t *p = pkt;
    *p++ = 0x08; *p++ = 0x00;
    *p++ = s_nav ? 0xFF : 0x00;
    *p++ = s_nav ? 0xFF : 0x00;
    static const uint8_t ra[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    memcpy(p, ra, 6); p += 6;
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    memcpy(p, mac, 6); p += 6;
    memcpy(p, mac, 6); p += 6;

    static uint16_t seq = 0;
    seq = (seq + 1) & 0x0FFF;
    *p++ = (uint8_t)((seq & 0x000F) << 4);
    *p++ = (uint8_t)(seq >> 4);

    static const uint8_t llc[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00};
    memcpy(p, llc, sizeof llc); p += sizeof llc;

    while ((size_t)(p - pkt) < FRAME_LEN) *p++ = 0xAA;
    return (size_t)(p - pkt);
}

static size_t build_frame(uint8_t *pkt)
{
    return s_mode ? build_data_pkt(pkt) : build_bcn_pkt(pkt);
}

static void tx_task(void *arg)
{
    static uint8_t frame[FRAME_LEN];
    size_t len = 0;
    while (1) {
        if (!s_run) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (len == 0) len = build_frame(frame);
        else {
            uint16_t seq = (uint16_t)(frame[22] >> 4) | (uint16_t)(frame[23] << 4);
            (void)seq;
            len = build_frame(frame);
        }
        esp_err_t err = esp_wifi_80211_tx(WIFI_IF_AP, frame, (int)len, true);
        if (err == ESP_OK) {
            s_ok++;
        } else {
            s_err++;
            vTaskDelay(1);
        }
    }
}

static void stats_task(void *arg)
{
    uint32_t last = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t ok = s_ok;
        s_fps = ok - last;
        last = ok;
        uint8_t ch = 0;
        wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
        esp_wifi_get_channel(&ch, &sec);
        printf("fps=%lu ok=%lu err=%lu ch=%d nav=%d run=%d\n",
               (unsigned long)s_fps, (unsigned long)ok, (unsigned long)s_err,
               ch, s_nav ? 1 : 0, s_run ? 1 : 0);
    }
}

static void cli_task(void *arg)
{
    char line[64];
    printf("commands: start | stop | status | nav 0|1\n");
    while (1) {
        if (fgets(line, sizeof(line), stdin) != NULL) {
            if (strncmp(line, "start", 5) == 0) {
                s_run = true;
                printf("start channel=%d len=%d nav=%d mode=%s\n", OCC_CHANNEL, FRAME_LEN, s_nav,
                       s_mode ? "data" : "bcn");
            } else if (strncmp(line, "stop", 4) == 0) {
                s_run = false;
                printf("stop ok=%lu err=%lu\n", (unsigned long)s_ok, (unsigned long)s_err);
            } else if (strncmp(line, "nav", 3) == 0) {
                s_nav = atoi(line + 3) != 0;
                printf("nav=%d\n", s_nav);
            } else if (strncmp(line, "mode", 4) == 0) {
                s_mode = strstr(line + 4, "data") ? 1 : 0;
                printf("mode=%s\n", s_mode ? "data" : "bcn");
            } else if (strncmp(line, "status", 6) == 0) {
                uint8_t ch = 0;
                wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
                esp_wifi_get_channel(&ch, &sec);
                printf("channel=%d actual=%d nav=%d mode=%s run=%d fps=%lu ok=%lu err=%lu\n",
                       OCC_CHANNEL, ch, s_nav, s_mode ? "data" : "bcn", s_run,
                       (unsigned long)s_fps, (unsigned long)s_ok, (unsigned long)s_err);
            } else if (line[0] != '\n' && line[0] != '\r' && line[0] != '\0') {
                printf("unknown\n");
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

static void wifi_init_ap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));

    esp_err_t cc = esp_wifi_set_country_code("CN", true);
    printf("country CN rc=%s\n", esp_err_to_name(cc));

    wifi_config_t ap = {0};
    const char *ssid = "occ13";
    memcpy(ap.ap.ssid, ssid, strlen(ssid));
    ap.ap.ssid_len = strlen(ssid);
    ap.ap.channel = OCC_CHANNEL;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.max_connection = 1;
    ap.ap.ssid_hidden = 1;
    ap.ap.beacon_interval = 1000;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));

    esp_err_t rr = esp_wifi_config_80211_tx_rate(WIFI_IF_AP, WIFI_PHY_RATE_1M_L);
    printf("rate 1M rc=%s\n", esp_err_to_name(rr));

    ESP_ERROR_CHECK(esp_wifi_start());

    esp_err_t bw = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW20);
    printf("bw20 rc=%s\n", esp_err_to_name(bw));
    esp_err_t ch = esp_wifi_set_channel(OCC_CHANNEL, WIFI_SECOND_CHAN_NONE);
    printf("channel %d rc=%s\n", OCC_CHANNEL, esp_err_to_name(ch));
    uint8_t ach = 0;
    wifi_second_chan_t asec = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&ach, &asec);
    printf("actual channel=%d\n", ach);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(80);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_ap();

    xTaskCreate(stats_task, "stats", 3072, NULL, 3, NULL);
    xTaskCreate(tx_task, "tx", 4096, NULL, 5, NULL);
    xTaskCreate(cli_task, "cli", 4096, NULL, 4, NULL);
}
