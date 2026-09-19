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

#ifdef __cplusplus
}
#endif
