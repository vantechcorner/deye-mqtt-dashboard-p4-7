# Blank-screen diagnosis — P4-Nano × Youyeetoo MIPI7

Date: 2026-09-09  
Test: `examples/mipi7_colorbar` on COM36

## What firmware proved

| Signal | Result | Meaning |
|--------|--------|---------|
| Boot / DSI PHY LDO3 | OK | Board + firmware path fine |
| Hardware color-bar API | Running (VERTICAL ↔ HORIZONTAL) | Host is generating MIPI video |
| I2C GT911 `@0x5D` | **Found** | FPC routes **3V3 + SDA/SCL** to the panel |
| I2C BL `@0x45` | Missing | Not a Waveshare DSI panel (expected) |

So this is **not** a total “cable unplugged” failure — touch power/I2C work.  
Video/backlight path is still wrong or incomplete.

## Likely root causes (ordered)

### 1. Connector / cable mismatch (most likely)

| Side | Typical connector |
|------|-------------------|
| Waveshare ESP32-P4-Nano LCD | **15-pin** Pi-style DSI (2-lane + I2C + 3V3) |
| Youyeetoo YYT-MIPI7LCD2203 | **30-pin 0.5 mm** FPC (designed for RK / Tinker / Firefly DSI) |

A Raspberry Pi “display cable” is normally **15↔15**. Bridging Nano’s fat 15-pin socket to Youyeetoo’s thin **30-pin** with an improvised Pi cable often:

- Maps only I2C + 3V3 (explains GT911 OK)
- Mis-maps or omits DSI data/clock pairs
- Omits panel **enable / reset** GPIOs present on the 30-pin side
- Omits **5 V** backlight supply if the panel expects it on the 30-pin harness

Youyeetoo docs: use a **30-pin, 0.5 mm, same-face (or reverse) FPC** matched to their motherboard DSI — not a Pi DSI ribbon.

### 2. Lane count

R1 Linux DT: `dsi,lanes = <4>`, `rockchip,lane-rate = <1000>`.  
Nano FPC: **2-lane only**. Even with a perfect adapter, the panel IC may refuse 2-lane video.

### 3. Incomplete panel init

Wiki `panel-init-sequence` looks **truncated** (only regs `0x80`–`0x86` + sleep-out/display-on). Full vendor init may be required before DPI color bars appear.

### 4. Backlight

No Waveshare-style I2C dimmer. If backlight needs 5 V / EN on pins that the Pi cable does not carry → **perfectly black** even if MIPI data were correct.

Quick physical check: in a dark room, look sideways at the glass — any faint glow = BL on / video maybe bad; totally dead glass = power/BL/cable first.

## What to do next (hardware)

1. Confirm cable type: photo of both ends (Nano 15-pin vs panel 30-pin) and any adapter board.
2. Prefer official Youyeetoo **30-pin** FPC + a proper **15↔30** adapter that documents DSI lane + 5V + I2C mapping — or use a Waveshare 7"/10.1" DSI panel that matches Nano’s 15-pin pinout.
3. Check whether the panel has a separate **5V / BL** wire or pad not connected.
4. If you have access to a full `rk3588s-lcd-yyt.dtsi` / vendor init dump, we can expand firmware init next.

## Software status

Colorbar firmware is fine for bring-up; blank image is expected until the electrical/mapping issues above are fixed. No MQTT/UI work until MIPI shows *something*.
