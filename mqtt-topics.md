# MQTT topic contract — Deye via IRIV IOC

**Broker base:** `iriv/ivt`  
**Subscribe pattern:** `iriv/ivt/#`  
**Payload:** JSON with numeric field `value` (engineering units already scaled by IRIV).

Example:

```json
{"value": 54.02}
```

Full topic = `iriv/ivt/` + `topicSuffix`.

| UI group | topicSuffix | Full topic | Unit | Notes |
|----------|-------------|------------|------|--------|
| Status | `status` | `iriv/ivt/status` | — | 0–4 operating status |
| Battery | `battery/voltage` | `iriv/ivt/battery/voltage` | V | |
| Battery | `battery/soc` | `iriv/ivt/battery/soc` | % | |
| Battery | `battery/power` | `iriv/ivt/battery/power` | W | signed |
| Battery | `battery/current` | `iriv/ivt/battery/current` | A | signed |
| Battery | `battery/temperature` | `iriv/ivt/battery/temperature` | °C | |
| Battery | `battery/charge_today` | `iriv/ivt/battery/charge_today` | kWh | Modbus 70 |
| Battery | `battery/discharge_today` | `iriv/ivt/battery/discharge_today` | kWh | Modbus 71 |
| PV1 | `pv1/voltage` | `iriv/ivt/pv1/voltage` | V | |
| PV1 | `pv1/current` | `iriv/ivt/pv1/current` | A | signed |
| PV1 | `pv1/power` | `iriv/ivt/pv1/power` | W | signed |
| PV2 | `pv2/voltage` | `iriv/ivt/pv2/voltage` | V | |
| PV2 | `pv2/current` | `iriv/ivt/pv2/current` | A | signed |
| PV2 | `pv2/power` | `iriv/ivt/pv2/power` | W | signed |
| PV | `pv/energy_today` | `iriv/ivt/pv/energy_today` | kWh | |
| Grid | `grid/frequency` | `iriv/ivt/grid/frequency` | Hz | |
| Grid | `grid/voltage` | `iriv/ivt/grid/voltage` | V | |
| Grid | `grid/current` | `iriv/ivt/grid/current` | A | signed |
| Grid | `grid/power_ct` | `iriv/ivt/grid/power_ct` | W | signed |
| Grid | `grid/buy_today` | `iriv/ivt/grid/buy_today` | kWh | |
| Grid | `grid/sell_today` | `iriv/ivt/grid/sell_today` | kWh | |
| Inverter | `inverter/temperature` | `iriv/ivt/inverter/temperature` | °C | |
| Inverter | `inverter/power` | `iriv/ivt/inverter/power` | W | signed |
| Inverter | `inverter/frequency` | `iriv/ivt/inverter/frequency` | Hz | |
| Load | `load/power` | `iriv/ivt/load/power` | W | signed |
| Load | `load/energy_today` | `iriv/ivt/load/energy_today` | kWh | |

## Operating status map

| value | Text |
|-------|------|
| 0 | Standby |
| 1 | Self-Test |
| 2 | Normal |
| 3 | Alarm |
| 4 | Fault |

## Derived metrics (compute on LCD)

| Name | Formula |
|------|---------|
| PV total power | `pv1/power` + `pv2/power` |

## Stale / offline

If broker disconnects or a topic has no update for longer than the UI freshness window, show placeholder (e.g. `—`) rather than freezing last value without indication.
