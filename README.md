# Deye MQTT dashboard — ESP32-P4-Nano + Youyeetoo 7" MIPI

Firmware **viewer** for Deye SG05/SG06 telemetry on:

- [Waveshare ESP32-P4-Nano](https://docs.waveshare.com/ESP32-P4-NANO) (ESP32-P4 + ESP32-C6 Wi-Fi, MIPI-DSI **2-lane**)
- [Youyeetoo YYT-MIPI7LCD2203](https://wiki.youyeetoo.com/en/x1/windows/MIPI7LCD) (7" MIPI, **1024×600**, GT911 touch)

Sibling of `deye-mqtt-dashboard-lcd-35` (SPI ST7796 480×320). Same MQTT contract; different SoC / BSP / UI layout.

## Critical hardware note (read first)

| Item | Youyeetoo R1 (Linux) bring-up | ESP32-P4-Nano |
|------|-------------------------------|---------------|
| DSI lanes | **4** (`dsi,lanes = <4>`) | **2** on FPC |
| Resolution | 1024×600 | same target |
| Touch | GT911 `@0x5d` | expect GT911 on LCD FPC I2C (GPIO7 SDA / GPIO8 SCL typical) |

R1 device-tree timings are captured in [reference/youyeetoo-mipi7-timings.md](reference/youyeetoo-mipi7-timings.md).  
**Bring-up risk:** a panel wired/configured for 4-lane on RK3588 may not run as-is on Nano’s 2-lane connector. Confirm pinout/adapter and whether the panel IC can operate in 2-lane mode before expecting image.

## MQTT contract

- Subscribe: `iriv/ivt/#`
- Payload: `{"value": <number>}`
- Map: [mqtt-topics.md](mqtt-topics.md)

Defaults (override in `menuconfig` / NVS): WiFi empty, MQTT host `192.168.3.249:1883`.

## Status

| Piece | State |
|-------|--------|
| Target `esp32p4` + sdkconfig defaults | Ready (rev &lt; v3 / min 100) |
| Telemetry store (ported from LCD-35) | Ready |
| MQTT + Wi-Fi STA | Ready via ESP-Hosted (`esp_wifi_remote` + C6 SDIO) |
| Display | **RPi Touch Display 2** 720×1280 ILI9881C (MCU @0x45 + RPi vendor init) |
| LVGL 9 UI | Portrait dashboard (Home / Batt / PV / Grid / Load), PSRAM double partial + DMA2D |

## Build (ESP-IDF ≥ 5.3, preferably 5.5.x with P4 support)

```powershell
$env:IDF_TOOLS_PATH = "D:\Espressif"
$env:IDF_PATH = "D:\Espressif\frameworks\esp-idf-v5.5.4"
$py = "D:\Espressif\tools\idf-python\3.11.2\python.exe"
$exports = & $py "$env:IDF_PATH\tools\activate.py" --export
. $exports
cd D:\Github\deye-mqtt-dashboard-p4-7
idf.py set-target esp32p4
idf.py menuconfig   # Deye P4 dashboard → WiFi / MQTT
idf.py build
idf.py -p COMx flash monitor
```

## Screen compatibility test (do this first)

Standalone color-bar firmware (Waveshare `13_Displaycolorbar` style, Youyeetoo timings):

→ [`examples/mipi7_colorbar/`](examples/mipi7_colorbar/)

```powershell
cd D:\Github\deye-mqtt-dashboard-p4-7\examples\mipi7_colorbar
idf.py set-target esp32p4
idf.py build
idf.py -p COMx flash monitor
```

Expect rotating hardware color bars if the Pi-style DSI cable + 2-lane panel path work.

**2026-09-09 bring-up:** firmware + GT911 I2C OK on COM36, but screen stayed blank —
see [reference/blank-screen-diagnosis.md](reference/blank-screen-diagnosis.md)
(15-pin Nano vs 30-pin Youyeetoo / 2-lane vs 4-lane / missing BL 5V).

RPi Touch Display 2 experiment: [`examples/rpi_td2_colorbar/`](examples/rpi_td2_colorbar/)
(ILI9881 720×1280). Flashed COM36; hung at panel install until DSI responds —
needs **DSI FFC + 5V J1** confirmed.

## Next bring-up steps

1. Confirm TD2 wiring (15-way DSI + 5V/GND on J1); look for `ili9881c: ID1: 0x98` then bars.
2. If ID OK but black → port Linux `rpi_7inch_init[]` into the example.
3. Preferred long-term panel: Waveshare 7/10.1 DSI (official Nano BSP).
4. MQTT / LVGL dashboard after display works.

## References

- Waveshare: https://docs.waveshare.com/ESP32-P4-NANO  
- Youyeetoo MIPI7 (X1): https://wiki.youyeetoo.com/en/x1/windows/MIPI7LCD  
- Youyeetoo R1 7" MIPI DT: https://wiki.youyeetoo.com/en/r1/OUHDMI#h-7-inch-mipi-screen  
- ESP-IDF MIPI DSI: https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/lcd/dsi_lcd.html  
