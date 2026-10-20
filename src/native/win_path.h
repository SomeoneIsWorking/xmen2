#ifndef X2_WIN_PATH_H
#define X2_WIN_PATH_H

/* Windows-to-host path and file-open policy is owned by win_path.cpp. Native
   bridges use this interface instead of growing their own drive-letter rules.
 */
const char *win_path(const char *input);
const char *k32_open_path(const char *guest_path, int for_write);
int k32_open_replaced(const char *guest_path, int for_write);
void k32_open_note(const char *guest_path, int ok, int replaced,
                   const char *host_path);
void k32_file_trace(const char *operation, const char *guest_path,
                    const char *host_path, const char *outcome);
int k32_file_gate_open(void);
/* A second overlay root, after X2_ASSETS, for files the port writes while it
   runs (control-channel scripts). */
void k32_set_generated_assets(const char *root);
void k32_asset_report(void);

#endif
