#ifndef X2_PROXY_SHADOW_SETTING_H
#define X2_PROXY_SHADOW_SETTING_H

unsigned char *shadow_setting_address(void);
int shadow_setting_install_query_override(int forced_value);
unsigned shadow_setting_forced_reads(void);
int shadow_setting_original_value(void);

#endif
