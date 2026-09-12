# Deye MQTT dashboard â€” ESP32-P4-Nano + RPi Touch Display 2

Firmware **viewer** for Deye SG05/SG06 telemetry on:

- [Waveshare ESP32-P4-Nano](https://docs.waveshare.com/ESP32-P4-NANO) (ESP32-P4 + ESP32-C6 Wi-Fi via ESP-Hosted, MIPI-DSI **2-lane**)
- **RPi Touch Display 2** (TD2): 720Ã—1280 ILI9881C, GT911 touch

Sibling of `deye-mqtt-dashboard-lcd-35` (SPI ST7796 480Ã—320). Same MQTT contract; different SoC / BSP / UI layout.

## SoftAP WiFi + MQTT provisioning

On first boot (no NVS WiFi SSID), or when you re-enter setup, the device starts an open SoftAP and a captive portal:

| Item | Value |
|------|--------|
| AP name | `Deye-P4-XXXX` (last 2 bytes of the SoftAP/Wi-Fi MAC) |
| Portal | [http://192.168.4.1](http://192.168.4.1) |
| Form fields | WiFi SSID / password; MQTT host / port / user / pass; base topic |

**Defaults** (pre-filled when empty / NVS unset):

- MQTT host `192.168.3.249`, port `1883`
- Topic `iriv/ivt`

Flow: connect phone to the AP â†’ open the portal â†’ Save â†’ device reboots into STA + MQTT.

**Re-enter setup** from the dashboard:

- **Setup** button in the mode toggle (always visible), or
- Tap the clock **5Ã—** within ~2.5s

Both set a NVS flag (`force_setup`) and reboot into SoftAP mode.

If the dashboard appears immediately after flash, NVS already has a WiFi SSID from prior testing — that is expected. Use **Setup** (or clock ×5) to force SoftAP again; erase NVS only if you want a clean first-boot experience.

**Backup branch** (pre-provisioning dashboard): `backup/pre-wifi-provisioning`

## UI modes

| Mode | Purpose |
|------|---------|
| **Simple** | Compact home overview |
| **Full** | Detailed PV / battery / grid / load |
| **HA** | Home-Assistant-style tile layout |
| **Synk** | Sunsynk-style power-flow diagram |

## MQTT contract

- Subscribe: `iriv/ivt/#`
- Payload: `{"value": <number>}`
- Map: [mqtt-topics.md](mqtt-topics.md)

## House load vs LOAD port

IRIV publishes two different watt topics. They are **not** Solarman “consumption” by themselves.

| Topic | Modbus | Meaning |
|-------|--------|---------|
| `inverter/power` | 175 | Signed AC **converter** power. Negative while the inverter draws from AC (e.g. charging). Do not show this as house load. |
| `load/power` | 178 | **LOAD port** (backup / essential) only |
| `grid/power_ct` | 172 | External CT. Positive = grid import |

**House total load** (Simple / Full **Load**, HA **Load Power**, Synk **DAILY LOAD**):

```
house_w = load/power + max(grid/power_ct, 0)
```

Add CT import only. When exporting (`grid/power_ct` < 0), house load is just the LOAD port.

**Synk inverter hub** (box above the inverter icon): LOAD port only — `load/power` and `load/current`. Not house total, not `inverter/power`.

Daily **kWh** on the Synk home bubble is separate: PV + grid buy + battery discharge − grid sell − battery charge (same idea as Home Assistant energy distribution).

## Status

| Piece | State |
|-------|--------|
| Target `esp32p4` + sdkconfig defaults | Ready (rev &lt; v3 / min 100) |
| SoftAP provisioning portal | Ready |
| Telemetry store (ported from LCD-35) | Ready |
| MQTT + Wi-Fi STA | Ready via ESP-Hosted (`esp_wifi_remote` + C6 SDIO) |
| Display | **RPi Touch Display 2** 720Ã—1280 ILI9881C |
| LVGL 9 UI | Portrait dashboard (Simple / Full / HA / Synk), PSRAM double partial + DMA2D |

## Flash prebuilt image (no build)

A merged flash image (bootloader + partition table + app) is in [`firmware/`](firmware/):

```powershell
esptool.py --chip esp32p4 -p COMx -b 460800 write_flash 0x0 firmware/deye-mqtt-dashboard-p4-7-full.bin
```

See [`firmware/README.md`](firmware/README.md) for the exact filename, version, and notes.

## Build from source (ESP-IDF â‰¥ 5.3, preferably 5.5.x with P4 support)

```powershell
$env:IDF_TOOLS_PATH = "D:\Espressif"
$env:IDF_PATH = "D:\Espressif\frameworks\esp-idf-v5.5.4"
$env:IDF_PYTHON_ENV_PATH = "D:\Espressif\python_env\idf5.5_py3.11_env"
$py = "D:\Espressif\tools\idf-python\3.11.2\python.exe"
$exports = & $py "$env:IDF_PATH\tools\activate.py" --export
. $exports
cd D:\Github\deye-mqtt-dashboard-p4-7
idf.py set-target esp32p4
idf.py menuconfig   # Deye P4 dashboard â†’ WiFi / MQTT defaults (leave WiFi empty)
idf.py build
idf.py -p COMx flash monitor
```

Optional: leave WiFi SSID empty in menuconfig; provision via SoftAP on device.

Rebuild the merged 0x0 image after a successful build:

```powershell
idf.py merge-bin -o firmware/deye-mqtt-dashboard-p4-7-full.bin
```

## Screen bring-up test

Optional color-bar firmware for TD2 wiring checks:

â†’ [`examples/rpi_td2_colorbar/`](examples/rpi_td2_colorbar/)

**Wiring:** TD2 needs a 15-way DSI FFC **and** **5V on J1** (panel stays dark without J1 power).

## Hardware brief

| Item | Note |
|------|------|
| MCU board | Waveshare ESP32-P4-Nano (P4 + C6 Wi-Fi) |
| Panel | RPi Touch Display 2, 720Ã—1280, 2-lane DSI |
| Touch | GT911 on LCD FPC I2C |

## References

- Waveshare: https://docs.waveshare.com/ESP32-P4-NANO  
- ESP-IDF MIPI DSI: https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/lcd/dsi_lcd.html  

