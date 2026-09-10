# Youyeetoo YYT-MIPI7LCD2203 — timings from R1 Linux DT

Source: [youyeetoo R1 OUHDMI — 7-inch MIPI Screen](https://wiki.youyeetoo.com/en/r1/OUHDMI#h-7-inch-mipi-screen)  
Device tree: `rk3588s-lcd-yyt.dtsi` (`dsi0` / `disp_timings0`).

## Panel

| Field | Value |
|-------|--------|
| Model | YYT-MIPI7LCD2203 |
| Resolution | **1024 × 600** |
| Color | RGB888 |
| Mode (RK) | Video + burst + LPM + EOT |
| Lanes (RK) | **4** |
| Lane rate (RK) | 1000 Mbps |

## Timing (`dsi0_timing0`)

| Parameter | Value |
|-----------|--------|
| `clock-frequency` (DCLK) | 51668640 Hz (~51.67 MHz) |
| `hactive` | 1024 |
| `vactive` | 600 |
| `hfront-porch` | 160 |
| `hback-porch` | 160 |
| `hsync-len` | 10 |
| `vfront-porch` | 12 |
| `vback-porch` | 23 |
| `vsync-len` | 10 |
| hsync / vsync active | 0 (not inverted) |
| de-active | 1 |
| pixelclk-active | 0 |

## Touch (R1 schematic → i2c3)

| Field | Value |
|-------|--------|
| Chip | Goodix GT911 (`goodix,gt9xx`) |
| I2C address | `0x5d` |
| max-x / max-y | 1024 / 600 |

On ESP32-P4-Nano the LCD FPC typically exposes I2C on **GPIO7 (SDA)** / **GPIO8 (SCL)** — map INT/RST from the Youyeetoo cable/pinout (do not copy RK GPIO numbers).

## ESP32-P4-Nano mapping notes

- Board DSI: **2-lane** only ([Waveshare docs](https://docs.waveshare.com/ESP32-P4-NANO)).
- Re-validate lane count and `panel-init-sequence` against a full DTS dump; the wiki snippet of init commands may be truncated.
- Prefer Ethernet on Nano for first MQTT bring-up if hosted Wi-Fi is not ready.
