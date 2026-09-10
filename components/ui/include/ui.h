#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(void);

/** Full-screen SoftAP setup instructions (AP SSID + portal URL). */
void ui_show_provisioning(const char *ap_ssid, const char *portal_url);

/** Register callback invoked when user re-enters setup (clock×5 or Setup). */
void ui_set_setup_request_cb(void (*cb)(void));

bool ui_is_provisioning(void);

#ifdef __cplusplus
}
#endif
