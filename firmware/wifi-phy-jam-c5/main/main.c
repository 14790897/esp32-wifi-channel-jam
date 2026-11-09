#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_phy_cert_test.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"

static volatile int s_running = 0;
static volatile int s_chan = 13;
static volatile int s_mode = 2;
static const char *mode_names[] = {"1M", "6M", "MCS0", "11AX-MCS0", "CW"};

static void tx_task(void *arg)
{
    int mode = s_mode;
    int chan = s_chan;
    esp_phy_test_start_stop(3);
    if (mode == 4) {
        printf("CW tone on ch=%d\n", chan);
        esp_phy_wifi_tx_tone(1, chan, 0);
        vTaskDelete(NULL);
        return;
    }
    esp_phy_wifi_rate_t rate;
    switch (mode) {
    case 0: rate = PHY_RATE_1M; break;
    case 1: rate = PHY_RATE_6M; break;
    case 2: rate = PHY_RATE_MCS0; break;
    default: rate = PHY_RATE_11AX_MCS0; break;
    }
    printf("PHY TX ch=%d rate=%s len=1500\n", chan, mode_names[mode]);
    esp_phy_wifi_tx(chan, rate, 0, 1500, 0, 0);
    printf("PHY TX returned\n");
    s_running = 0;
    vTaskDelete(NULL);
}

static void stop_task(void *arg)
{
    int sec = (int)(intptr_t)arg;
    vTaskDelay(pdMS_TO_TICKS(sec * 1000));
    if (s_mode == 4) {
        esp_phy_wifi_tx_tone(0, s_chan, 0);
    }
    esp_phy_test_start_stop(0);
    s_running = 0;
    printf("auto stop after %ds\n", sec);
    vTaskDelete(NULL);
}

static void cli_task(void *arg)
{
    char line[64];
    printf("phyjam: start <ch> <sec> [mode] | stop | status | mode 0..4\n");
    while (1) {
        if (fgets(line, sizeof line, stdin)) {
            if (strncmp(line, "start", 5) == 0) {
                int ch = 0, sec = 10, mode = s_mode;
                sscanf(line + 5, "%d %d %d", &ch, &sec, &mode);
                if (ch <= 0 || ch > 196) ch = 13;
                if (sec < 1 || sec > 60) sec = 10;
                if (mode < 0 || mode > 4) mode = 2;
                if (!s_running) {
                    s_chan = ch;
                    s_mode = mode;
                    s_running = 1;
                    xTaskCreate(tx_task, "tx", 4096, NULL, 1, NULL);
                    xTaskCreate(stop_task, "stop", 3072, (void *)(intptr_t)sec, 6, NULL);
                    printf("started ch=%d sec=%d mode=%s\n", ch, sec, mode_names[mode]);
                } else {
                    printf("already running\n");
                }
            } else if (strncmp(line, "stop", 4) == 0) {
                if (s_mode == 4) {
                    esp_phy_wifi_tx_tone(0, s_chan, 0);
                }
                esp_phy_test_start_stop(0);
                s_running = 0;
                printf("stop sent\n");
            } else if (strncmp(line, "mode", 4) == 0) {
                int m = atoi(line + 4);
                if (m >= 0 && m <= 4) {
                    s_mode = m;
                    printf("mode=%s\n", mode_names[m]);
                }
            } else if (strncmp(line, "status", 6) == 0) {
                printf("running=%d ch=%d mode=%s\n", s_running, s_chan, mode_names[s_mode]);
            } else if (line[0] != '\n' && line[0] != '\r' && line[0] != '\0') {
                printf("unknown\n");
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

    esp_task_wdt_deinit();
    esp_wifi_power_domain_on();
    esp_phy_rftest_config(1);
    esp_phy_rftest_init();
    printf("rftest init done\n");

    xTaskCreate(cli_task, "cli", 4096, NULL, 4, NULL);
}
