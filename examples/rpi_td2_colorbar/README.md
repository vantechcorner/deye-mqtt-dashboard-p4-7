# RPi Touch Display 2 (7") colorbar — experimental

Bring-up test for **Raspberry Pi Touch Display 2 7″** on Waveshare **ESP32-P4-Nano**.

- Panel IC: ILI9881 (`raspberrypi,dsi-7inch`)
- Resolution / timings: **720×1280**, from Linux `rpi_7inch_default_mode`
- Driver: `espressif/esp_lcd_ili9881c` (default init first)

## Wiring (required)

1. **15-way DSI FFC** between Nano `LCD` and Touch Display 2 DSI (Pi 4 style cable).
2. **5V GPIO power cable** into display **J1** (5V + GND). Without this the panel stays dark.

## Build / flash

```powershell
cd D:\Github\deye-mqtt-dashboard-p4-7\examples\rpi_td2_colorbar
idf.py set-target esp32p4
idf.py -p COMx flash monitor
```

## Pass / fail

| Log / screen | Meaning |
|--------------|---------|
| `TD2 MCU ID=0x01` | 7″ MCU @ 0x45 OK; 5V J1 + I2C OK |
| `ili9881c: ID1: 0x98...` | Panel answering DSI after MCU power/reset |
| Color bars / backlight glow | Bring-up success |
| MCU OK but no glow | Check PWM / POWERON writes |
| Hang before ID | MCU not powered (check 5V on J1) |

This is **unsupported** by Raspberry Pi on ESP32; treat as a lab bring-up tool.
