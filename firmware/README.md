# Prebuilt firmware

| File | Description |
|------|-------------|
| `deye-mqtt-dashboard-p4-7-full.bin` | Stable alias — merged image flashable from **0x0** |
| `deye-mqtt-dashboard-p4-7-full-0.1.0-YYYYMMDD.bin` | Versioned copy of the same image |
| `deye-mqtt-dashboard-p4-7-app.bin` | App partition only (offset `0x10000`) |
| `VERSION.txt` | Build metadata |
| `flash_args` | Reference args from the IDF build (paths relative to `build/`) |

## Flash full image (recommended)

```powershell
esptool.py --chip esp32p4 -p COMx -b 460800 write_flash 0x0 deye-mqtt-dashboard-p4-7-full.bin
```

Or from the repo root:

```powershell
esptool.py --chip esp32p4 -p COMx -b 460800 write_flash 0x0 firmware/deye-mqtt-dashboard-p4-7-full.bin
```

Requires ESP-IDF / esptool with ESP32-P4 support. Board: Waveshare ESP32-P4-Nano + RPi Touch Display 2.

## Rebuild

From an activated ESP-IDF 5.5.x shell:

```powershell
idf.py build
python -m esptool --chip esp32p4 merge_bin -o ../firmware/deye-mqtt-dashboard-p4-7-full.bin `
  --flash_mode dio --flash_freq 80m --flash_size 16MB `
  0x2000 bootloader/bootloader.bin `
  0x8000 partition_table/partition-table.bin `
  0x10000 deye-mqtt-dashboard-p4-7.bin
```

(Run the `merge_bin` line from the `build/` directory, or use absolute `-o` paths.)
