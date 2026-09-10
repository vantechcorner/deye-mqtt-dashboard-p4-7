#pragma once

#include "lvgl.h"
#include "telemetry.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_sunsynk_build(lv_obj_t *root);
void ui_sunsynk_update(const telemetry_snapshot_t *snap);

#ifdef __cplusplus
}
#endif
