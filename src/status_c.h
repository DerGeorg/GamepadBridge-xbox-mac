/*
 * Copyright (C) 2026 GamepadBridge contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The plain-C half of the status board, kept separate from status.h because
 * the Swift bridging header is compiled as C and cannot see <string> or a
 * namespace.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void gpb_status_connection(char *buffer, long capacity);
void gpb_status_battery(char *buffer, long capacity);
int  gpb_status_controller_present(void);

/*
 * Both are expressed as signals, so the menu drives exactly the same paths as
 * Ctrl-C and `kill -USR1`. There is no second shutdown route to keep correct.
 */
void gpb_request_pairing(void);
void gpb_request_quit(void);

// 1 once Input Monitoring is granted. Polled by the permission panel so it
// can close itself and restart the app rather than leaving the user to.
int gpb_permission_granted(void);

// Set before gpb_menubar_run when the permission is missing.
void gpb_set_needs_permission(int needed);
int  gpb_needs_permission(void);

// One wording, used by both the window and the terminal build, so the two
// cannot drift into saying different things.
const char *gpb_permission_message(void);
const char *gpb_permission_settings_url(void);

#ifdef __cplusplus
}
#endif
