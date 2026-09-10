# MIPI7 colorbar — P4-Nano × Youyeetoo 7"

Standalone bring-up test (no MQTT / LVGL). Modeled on Waveshare
[`13_Displaycolorbar`](https://github.com/waveshareteam/esp32-p4-platform/tree/main/examples/esp-idf/13_Displaycolorbar):
enable DSI PHY → init panel → `esp_lcd_dpi_panel_set_pattern()`.

Waveshare BSP LCD presets are for **their** panels (JD9365 / ILI9881C / …).
Youyeetoo `YYT-MIPI7LCD2203` is not in that list, so this example uses R1 wiki
timings (1024×600) on Nano’s **2-lane** FPC instead of `waveshare/esp32_p4_platform`.

## Hardware

- Waveshare **ESP32-P4-Nano**
- Youyeetoo **YYT-MIPI7LCD2203** (7" MIPI, GT911)
- Raspberry Pi-style DSI ribbon (fat ↔ thin adapter as needed)

## Build / flash

```powershell
$env:IDF_TOOLS_PATH = "D:\Espressif"
$env:IDF_PATH = "D:\Espressif\frameworks\esp-idf-v5.5.4"
$py = "D:\Espressif\tools\idf-python\3.11.2\python.exe"
$exports = & $py "$env:IDF_PATH\tools\activate.py" --export
. $exports

cd D:\Github\deye-mqtt-dashboard-p4-7\examples\mipi7_colorbar
idf.py set-target esp32p4
idf.py build
idf.py -p COMx flash monitor
```

## Pass / fail

| Observation | Meaning |
|-------------|---------|
| Serial: `MIPI DSI PHY LDO3 … OK` then `Pattern: VERTICAL` | Firmware path running |
| I2C finds `0x5D` or `0x14` | Touch lines on cable likely OK |
| I2C finds `0x45` | Waveshare-style BL chip present (not required for Youyeetoo) |
| Visible color bars | **Cable + DSI lanes + panel video path look compatible** |
| Black / white / no image | See [blank-screen diagnosis](../../reference/blank-screen-diagnosis.md) |

### Observed on COM36 (2026-09-09)

Firmware OK + GT911 `@0x5D` found + color-bar patterns cycling, but **panel stayed blank**.

Root cause is almost certainly **electrical / connector**, not the test app:

- Nano: **15-pin** Pi-style DSI (2-lane + I2C + 3V3)
- Youyeetoo MIPI7: **30-pin 0.5 mm** FPC (vendor cable for RK/Tinker/Firefly)
- R1 DT: **4 DSI lanes**; Nano: **2 lanes**
- No I2C backlight chip `@0x45` (Waveshare-only)

A temporary Raspberry Pi display cable often maps touch I2C but not a full DSI+5V+EN set.

## Next

If bars appear → port the same bring-up into `components/bsp_p4_mipi7` for the dashboard.  
If not → capture serial log + whether GT911 shows on I2C before chasing UI/MQTT.
