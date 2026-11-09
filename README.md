# ESP32 Wi-Fi Channel Jam (PHY 连续发射)

用 ESP32 的物理层认证测试模式连续发射，把 2.4GHz 的某个信道完全占满，让同信道的 AP 发不出 beacon；已经连上的 STA 在约 9 秒后因 **beacon 超时（reason=200）** 掉线。停发后 beacon 恢复，STA 自动重连。

> ⚠️ **仅用于自己的设备和自己的网络。** 无线电发射受当地法规约束；请勿用于干扰他人网络、公共场所或任何未授权的目标。本项目用于安全研究与教学实验。

---

## 原理

1. **Wi-Fi 连接靠 beacon 维持**：AP 每 100ms 发一个 beacon（10/s）。STA 每收到一个就把掉线倒计时清零；连续约 9 秒收不到，就报 `reason=200`，连接断开。
2. **普通 CSMA 大流量打不断**：Wi-Fi 是“先听后说”。发完一帧要等 DIFS + 随机退避，帧与帧之间总有 0.1–0.3ms 的缝隙；beacon 很小，正好从缝隙里挤过去，STA 的倒计时不断清零。
3. **PHY 连续发射把缝隙填满**：`esp_phy_wifi_tx()` 在认证测试模式下连续发包，不等 CCA、不等退避，帧间没有缝隙。信道 100% 忙，AP 每次想发 beacon 都只能继续等；beacon 变成 0/s，STA 的倒计时走到 9 秒，报 `reason=200` 掉线。
4. 停发后 beacon 立即恢复，STA 自动重连（`got ip`）。

## 实测结果（ESP32-C3 / C6，信道 13）

| 指标 | 攻击前 | 占道中 | 停发后 |
|---|---|---|---|
| JAMAP13 beacon | 10/s | 0/s | 10/s |
| 每秒唯一 SSID 数 | 76 | 0 | 76 |
| 手机扫描 ch13 条目 | 26 | 0 | 26 |
| 被连接 STA | connected | `wch=-1` → `reason=200` | `got ip`，自动重连 |

对照实验：

| 方式 | beacon 速率 | 结果 |
|---|---|---|
| CSMA 大帧（1500B @ 1Mbps） | 仍 10/s | 打不断（缝隙漏过） |
| 单播 NAV 帧（Duration=32767） | ~3.8/s | 仍不够触发超时 |
| **PHY 连续发射（本项目）** | **0/s** | **约 9 秒后 reason=200 掉线** |

## 硬件

- 2 × ESP32-C3 或 ESP32-C6：PHY 占道板
- 1 × ESP32-C3：假 SSID 机（75 个 Wi-Fi 名字，ch13）
- 1 × ESP32-C6：AP（`victim-ap`）
- 1 × ESP32-C6：STA（`victim-sta`）
- 手机连 5GHz，避免影响手机自己的网络

## 构建

- ESP-IDF **v6.1** 测试通过（更早的 5.x 版本可能也可用）
- 需要开启 `CONFIG_ESP_PHY_ENABLE_CERT_TEST=y`（各 `sdkconfig.defaults` 已包含）
- 先设置环境变量 `IDF_PATH` 指向 ESP-IDF 目录，再运行各固件目录下的 `build_flash_*.bat`；也可以手动：

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p COMx flash
```

`.bat` 里的 COM 口按实际接线修改。

## 使用

### PHY 占道板（`wifi-phy-jam` / `wifi-phy-jam-c6`）

串口（USB Serial/JTAG）命令：

```
start [ch] [sec]   # 例如 start 13 10；默认 ch13，10 秒自动停
stop
status
```

占道期间板子会独占射频，无法同时做普通 Wi-Fi。

### ch13 beacon 扫描器（`wifi-beacon-scan`）

每秒输出：

```
bcn/s=... jam13=... other=... uniq=...
```

`jam13` 是目标 AP 每秒收到的 beacon 数；`uniq` 是该秒内不同 SSID 的数量。

### 假 SSID 机（`fake-ssid-lab`）

上电后自动在 ch13 广播 75 个假 Wi-Fi 名字；串口命令 `start [n]` / `stop` / `rate` / `status`。

### Victim AP / STA（`victim-ap` / `victim-sta`）

- `victim-ap`：建立开放 AP `JAMAP13`（ch13），带 UDP 服务
- `victim-sta`：连接该 AP，打印 `wch`（当前信道）、`rssi`、`reason`、`got ip`

两个示例里还包含 BLE 代码，只是作为额外负载；你可以用任意 AP/STA（路由器 + 手机/电脑）替代。

### 测试脚本（`tools/`）

Python + pyserial（以及 `adb`，用于手机截图/刷新列表）。运行前把脚本顶部的 COM 口改成你的接线：

- `full_attack_test.py`：两块占道 + 假 SSID + 扫描器 + victim，完整复现
- `fake_ssid_test.py`：单块占道 + 假 SSID
- `phy_test.py`：基础占道测试
- `final_test.py`：自动停版本
- `occ_scan_test.py`：`wifi-occupy` 的 bcn/uniq 记录

## 目录

```
firmware/
  wifi-phy-jam/       C3 PHY 连续发射（核心）
  wifi-phy-jam-c6/    C6 版本
  wifi-occupy/        CSMA / NAV 对照实验
  wifi-beacon-scan/   ch13 beacon 扫描器
  fake-ssid-lab/      75 个假 SSID
  victim-ap/          AP 端示例
  victim-sta/         STA 端示例
tools/                测试脚本
```

## 注意

- 占道期间同信道的**所有**设备都会受影响（包括你自己的路由器，如果 2.4G 也在 ch13）。测试尽量短、可控。
- PHY 测试模式属于认证测试功能，会独占射频。
- 不要在公共场所或他人网络上使用；遵守当地无线电法规。

## License

MIT

---

## English summary

ESP32 Wi-Fi channel-occupancy demo. It uses the PHY certification test mode (`esp_phy_wifi_tx`) to send back-to-back 1500-byte frames at 1 Mbps with **no CSMA gaps**. The AP cannot transmit beacons, so a connected STA times out after ~9 s with `reason=200` and disconnects. After stopping, beacons resume and the STA reconnects. The repo includes the jammer firmware (C3/C6), a beacon scanner, a fake-SSID lab, victim AP/STA examples, and Python test tools. For authorized testing on your own devices and networks only; comply with local radio regulations.
