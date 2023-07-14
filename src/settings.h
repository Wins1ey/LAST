#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <jansson.h>

typedef struct {
    const char* name;
    int value;
    const char* description;
} Setting;

void last_update_setting(const char *section, const char *setting, json_t *value, json_t *description);
json_t *get_setting_value(const char *section, const char *setting);
Setting* get_section_settings(const char* section, int* num_settings);

#endif /* __SETTINGS_H__ */