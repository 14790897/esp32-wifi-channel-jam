#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "nvs_flash.h"

#define MAX_SSIDS       512
#define SSID_MAX_LEN    32
#define FRAME_MAX       160
#define DEFAULT_N       0

#ifndef BOARD_INDEX
#define BOARD_INDEX     0
#endif
#ifndef BOARD_CHANNEL
#define BOARD_CHANNEL   6
#endif
#ifndef BOARD_BW40
#define BOARD_BW40      0
#endif
#ifndef BOARD_SECOND_CHAN
#define BOARD_SECOND_CHAN WIFI_SECOND_CHAN_NONE
#endif
#define BOARD_COUNT     2

static char s_names[MAX_SSIDS][SSID_MAX_LEN + 1];
static volatile int s_n = DEFAULT_N;
static volatile bool s_running = false;
static volatile bool s_task_alive = false;
static volatile uint32_t s_ok = 0;
static volatile uint32_t s_err = 0;
static volatile uint32_t s_fps = 0;
#if BOARD_BW40
static wifi_phy_rate_t s_rate = WIFI_PHY_RATE_MCS0_LGI;
static const char *s_rate_name = "MCS0";
#else
static wifi_phy_rate_t s_rate = WIFI_PHY_RATE_24M;
static const char *s_rate_name = "24M";
#endif

static const char *k_funny_ssids[] = {
    "隔壁老王的WiFi", "连了会变帅", "连了会变穷", "密码是八个8",
    "此WiFi已欠费", "网速比奶奶慢", "信号在冰箱里", "路由器在厕所",
    "别问就是没网", "连上算我输", "你家路由该换了", "蹭网可耻",
    "内有恶犬", "村口厕所没纸", "全村希望", "来都来了连一下",
    "禁止套娃", "小心地滑", "前方施工绕行", "老板跑路了",
    "这家没网", "你猜密码", "猜对也不给连", "免费但没网",
    "5G比4G慢", "我不是WiFi", "你连不上", "信号满格没网",
    "隔壁蹭网老王", "全村最慢", "已读不回", "摸鱼专用网",
    "上班勿连", "老板在看着", "这不是WiFi", "连了就加班",
    "断网保平安", "佛系WiFi", "随缘连", "密码在梦里",
    "梦里啥都有", "别连我", "连我必掉线", "掉线王",
    "延迟999", "丢包百分百", "网络已圆寂", "施主请回吧",
    "此网已出家", "无网胜有网", "信号靠想象", "假装有网",
    "空气WiFi", "画饼充饥网", "望梅止渴网", "薛定谔的网",
    "量子波动网", "玄学WiFi", "祖传WiFi", "百年老网",
    "古董WiFi", "博物馆专供", "文物级网络", "别碰会碎",
    "一碰就断", "豆腐渣网络", "纸糊的信号", "吹弹可破",
    "弱不禁风", "风一吹就断", "下雨就断网", "打雷必掉线",
    "晴天限定", "仅限室内", "出门就断", "阳台专享",
    "厕所满格", "厨房信号", "卧室专用", "客厅勿扰",
    "沙发专座", "被窝WiFi", "床底信号", "冰箱后面",
    "电视柜里", "鞋柜WiFi", "猫窝专用", "狗子热点",
    "猫踩出来的网", "狗啃过的路由",
    "TellMyWifiLoveHer", "PrettyFlyForAWiFi", "DropItLikeItsHotspot",
    "NotTheWiFiYoureLookingFor", "GetOffMyLAN", "TheLANBeforeTime",
    "PingMeMaybe", "ItHurtsWhenIP", "DontStealMyWiFi",
    "FreeWiFiNoInternet", "Error404WiFiNotFound", "ConnectAndRegret",
    "SlowInternetInside", "LoadingPleaseWait", "RouterIHardlyKnowHer",
    "WiFightClub", "ThePasswordIs1234", "NoMoreMrWiFi",
    "SilenceOfTheLANs", "LordOfThePings", "GameOfThronesWiFi",
    "StrangerThingsWiFi", "TheWalkingDeadWiFi", "BreakingBadWiFi",
    "BetterCallWiFi", "HouseOfCardsWiFi", "OrangeIsTheNewWiFi",
    "HowIMetYourWiFi", "TheBigBangWiFi", "FriendsWiFi",
    "SeinfeldWiFi", "TheOfficeWiFi", "ParksAndRecWiFi",
    "Brooklyn99WiFi", "RickAndMortyWiFi", "SouthParkWiFi",
    "SpongeBobWiFi", "PokemonWiFi", "MarioWiFi",
    "ZeldaWiFi", "MinecraftWiFi", "FortniteWiFi",
    "AmongUsWiFi", "AmongUsImposter", "SusWiFi",
    "VerySusWiFi", "YeetWiFi", "SheeshWiFi",
    "BruhWiFi", "VibesWiFi", "LowBatteryWiFi",
    "ChargingWiFi", "1PercentWiFi", "NoSignalWiFi",
    "SignalLost", "Buffering", "Loading",
    "PleaseWait", "TryAgainLater", "ComeBackTomorrow",
};

static int s_name_count = 0;

static void build_names(void)
{
    int full = (int)(sizeof(k_funny_ssids) / sizeof(k_funny_ssids[0]));
    s_name_count = 0;
    for (int i = BOARD_INDEX; i < full && s_name_count < MAX_SSIDS; i += BOARD_COUNT) {
        strncpy(s_names[s_name_count], k_funny_ssids[i], SSID_MAX_LEN);
        s_names[s_name_count][SSID_MAX_LEN] = '\0';
        s_name_count++;
    }
    printf("board=%d/%d channel=%d names=%d\n", BOARD_INDEX, BOARD_COUNT, BOARD_CHANNEL, s_name_count);
}

static size_t build_frame(int idx, uint8_t *pkt)
{
    const char *ssid = s_names[idx];
    size_t slen = strlen(ssid);
    if (slen > SSID_MAX_LEN) slen = SSID_MAX_LEN;

    uint8_t *p = pkt;
    *p++ = 0x80; *p++ = 0x00;               // beacon, flags 0
    *p++ = 0x00; *p++ = 0x00;               // duration
    memset(p, 0xff, 6); p += 6;             // addr1 broadcast
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < slen; i++) {
        h ^= (uint8_t)ssid[i];
        h *= 16777619u;
    }
    uint8_t bssid[6] = {0x02, 0x00, 0x00, (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h};
    memcpy(p, bssid, 6); p += 6;            // addr2
    memcpy(p, bssid, 6); p += 6;            // addr3

    static uint16_t seq = 0;
    seq = (seq + 1) & 0x0FFF;
    *p++ = (uint8_t)((seq & 0x000F) << 4);
    *p++ = (uint8_t)(seq >> 4);

    static uint64_t tsf = 0;
    tsf += 100000;
    memcpy(p, &tsf, 8); p += 8;

    *p++ = 0x64; *p++ = 0x00;               // beacon interval 100 TU
    *p++ = 0x11; *p++ = 0x04;               // ESS + Privacy + ShortSlot

    *p++ = 0x00; *p++ = (uint8_t)slen;      // SSID IE
    memcpy(p, ssid, slen); p += slen;

    static const uint8_t rates_ie[] = {0x01, 0x08, 0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C};
    memcpy(p, rates_ie, sizeof(rates_ie)); p += sizeof(rates_ie);

    *p++ = 0x03; *p++ = 0x01; *p++ = BOARD_CHANNEL;   // DS parameter

    static const uint8_t tim_ie[] = {0x05, 0x04, 0x00, 0x01, 0x00, 0x00};
    memcpy(p, tim_ie, sizeof(tim_ie)); p += sizeof(tim_ie);

    static const uint8_t rsn_ie[] = {
        0x30, 0x14,
        0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x04,
        0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x04,
        0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x02,
        0x00, 0x00,
    };
    memcpy(p, rsn_ie, sizeof(rsn_ie)); p += sizeof(rsn_ie);

    return (size_t)(p - pkt);
}

static void tx_task(void *arg)
{
    uint8_t frame[FRAME_MAX];
    s_task_alive = true;
    while (s_running) {
        for (int i = 0; i < s_n && s_running; i++) {
            size_t len = build_frame(i, frame);
            esp_err_t err = esp_wifi_80211_tx(WIFI_IF_AP, frame, len, false);
            if (err == ESP_OK) {
                s_ok++;
            } else {
                s_err++;
                if (err == ESP_ERR_NO_MEM || err == ESP_ERR_INVALID_STATE) {
                    vTaskDelay(1);
                }
            }
            vTaskDelay(1);
        }
    }
    s_task_alive = false;
    vTaskDelete(NULL);
}

static void start_spam(int n)
{
    if (s_running || s_task_alive) {
        printf("already running (n=%d)\n", s_n);
        return;
    }
    if (n <= 0 || n > s_name_count) n = s_name_count;
    s_n = n;
    s_ok = 0;
    s_err = 0;
    s_fps = 0;
    s_running = true;
    xTaskCreate(tx_task, "tx", 4096, NULL, 5, NULL);
    printf("started n=%d rate=%s channel=%d\n", s_n, s_rate_name, BOARD_CHANNEL);
}

static void stop_spam(void)
{
    if (!s_running) {
        printf("not running\n");
        return;
    }
    s_running = false;
    for (int i = 0; i < 500 && s_task_alive; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    printf("stopped ok=%lu err=%lu\n", (unsigned long)s_ok, (unsigned long)s_err);
}

static void set_rate(int mbit)
{
    switch (mbit) {
    case 6:  s_rate = WIFI_PHY_RATE_6M;  s_rate_name = "6M";  break;
    case 12: s_rate = WIFI_PHY_RATE_12M; s_rate_name = "12M"; break;
    case 24: s_rate = WIFI_PHY_RATE_24M; s_rate_name = "24M"; break;
    case 48: s_rate = WIFI_PHY_RATE_48M; s_rate_name = "48M"; break;
    case 54: s_rate = WIFI_PHY_RATE_54M; s_rate_name = "54M"; break;
    default: s_rate = WIFI_PHY_RATE_1M_L; s_rate_name = "1M"; break;
    }
    esp_err_t err = esp_wifi_config_80211_tx_rate(WIFI_IF_AP, s_rate);
    printf("rate=%s (%s)\n", s_rate_name, esp_err_to_name(err));
}

static void stats_task(void *arg)
{
    uint32_t last = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t ok = s_ok;
        s_fps = ok - last;
        last = ok;
        printf("fps=%lu ok=%lu err=%lu n=%d rate=%s run=%d\n",
               (unsigned long)s_fps, (unsigned long)ok, (unsigned long)s_err,
               s_n, s_rate_name, s_running ? 1 : 0);
    }
}

static void cli_task(void *arg)
{
    char line[64];
    printf("commands: start [n] | stop | rate 1|6 | status | help\n");
    while (1) {
        if (fgets(line, sizeof(line), stdin) != NULL) {
            if (strncmp(line, "start", 5) == 0) {
                int n = atoi(line + 5);
                start_spam(n > 0 ? n : s_n);
            } else if (strncmp(line, "stop", 4) == 0) {
                stop_spam();
            } else if (strncmp(line, "rate", 4) == 0) {
                set_rate(atoi(line + 4));
            } else if (strncmp(line, "status", 6) == 0) {
                printf("n=%d rate=%s run=%d fps=%lu ok=%lu err=%lu\n",
                       s_n, s_rate_name, s_running ? 1 : 0,
                       (unsigned long)s_fps, (unsigned long)s_ok, (unsigned long)s_err);
            } else if (strncmp(line, "help", 4) == 0) {
                printf("start [n] | stop | rate 1|6 | status | help\n");
            } else if (line[0] != '\n' && line[0] != '\r' && line[0] != '\0') {
                printf("unknown command\n");
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
    printf("country rc=%s\n", esp_err_to_name(cc));

    wifi_config_t ap = {0};
    const char *ssid = "c3lab";
    memcpy(ap.ap.ssid, ssid, strlen(ssid));
    ap.ap.ssid_len = strlen(ssid);
    ap.ap.channel = BOARD_CHANNEL;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.max_connection = 1;
    ap.ap.ssid_hidden = 1;
    ap.ap.beacon_interval = 1000;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
#if BOARD_BW40
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW40));
    ESP_ERROR_CHECK(esp_wifi_set_channel(BOARD_CHANNEL, BOARD_SECOND_CHAN));
#else
    ESP_ERROR_CHECK(esp_wifi_set_channel(BOARD_CHANNEL, WIFI_SECOND_CHAN_NONE));
#endif
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(80);
    esp_wifi_config_80211_tx_rate(WIFI_IF_AP, s_rate);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    build_names();
    wifi_init_ap();

    xTaskCreate(stats_task, "stats", 3072, NULL, 3, NULL);
    xTaskCreate(cli_task, "cli", 4096, NULL, 4, NULL);
    start_spam(DEFAULT_N);
}
