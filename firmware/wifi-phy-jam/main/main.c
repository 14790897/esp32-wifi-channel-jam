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

static void tx_task(void *arg)
{
    int chan = (int)(intptr_t)arg;
    printf("PHY TX start ch=%d\n", chan);
    esp_phy_test_start_stop(3);
    esp_phy_wifi_tx(chan, PHY_RATE_1M, 0, 1500, 0, 0);
    printf("PHY TX returned\n");
    s_running = 0;
    vTaskDelete(NULL);
}

static void stop_task(void *arg)
{
    int sec = (int)(intptr_t)arg;
    vTaskDelay(pdMS_TO_TICKS(sec * 1000));
    esp_phy_test_start_stop(0);
    s_running = 0;
    printf("auto stop after %ds\n", sec);
    vTaskDelete(NULL);
}

static void cli_task(void *arg)
{
    char line[64];
    printf("phyjam: start [ch] | stop | status\n");
    while (1) {
        if (fgets(line, sizeof line, stdin)) {
            if (strncmp(line, "start", 5) == 0) {
                int ch = 0, sec = 10;
                sscanf(line + 5, "%d %d", &ch, &sec);
                if (ch <= 0 || ch > 14) ch = 13;
                if (sec < 1 || sec > 60) sec = 10;
                if (!s_running) {
                    s_chan = ch;
                    s_running = 1;
                    xTaskCreate(tx_task, "tx", 4096, (void *)(intptr_t)ch, 1, NULL);
                    xTaskCreate(stop_task, "stop", 3072, (void *)(intptr_t)sec, 6, NULL);
                    printf("started ch=%d sec=%d\n", ch, sec);
                } else {
                    printf("already running\n");
                }
            } else if (strncmp(line, "stop", 4) == 0) {
                esp_phy_test_start_stop(0);
                s_running = 0;
                printf("stop sent\n");
            } else if (strncmp(line, "status", 6) == 0) {
                printf("running=%d ch=%d\n", s_running, s_chan);
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
