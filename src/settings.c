#include <stdio.h>
#include <pwd.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>

#include <jansson.h>

#include "settings.h"

char *get_settings_path()
{
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    if (pw == NULL)
    {
        printf("Failed to get user information.\n");
        return NULL;
    }
    
    return strcat(pw->pw_dir, "/.last/settings.json");
}

void last_update_setting(const char *section, const char *setting, json_t *value, json_t *description)
{
    char *settings_path = get_settings_path();

    // Load existing settings
    json_t *root = NULL;
    FILE *file = fopen(settings_path, "r");
    if (file)
    {
        json_error_t error;
        root = json_loadf(file, 0, &error);
        fclose(file);
        if (!root)
        {
            printf("Failed to load settings: %s\n", error.text);
            return;
        }
    }
    else
    {
        // If file doesn't exist, create a new settings object
        root = json_object();
    }

    // Update specific setting
    json_t *last_obj = json_object_get(root, section);
    if (!last_obj)
    {
        last_obj = json_object();
        json_object_set(root, section, last_obj);
    }
    
    json_t *setting_obj = json_object();
    json_object_set(setting_obj, "value", value);
    json_object_set(setting_obj, "description", description);
    json_object_set(last_obj, setting, setting_obj);

    // Save updated settings back to the file
    FILE *output_file = fopen(settings_path, "w");
    if (output_file)
    {
        json_dumpf(root, output_file, JSON_INDENT(4));
        fclose(output_file);
    }
    else
    {
        printf("Failed to save settings to %s\n", settings_path);
    }

    json_decref(root);
}

json_t *load_settings()
{
    char *settings_path = get_settings_path();
    FILE *file = fopen(settings_path, "r");
    if (file)
    {
        json_error_t error;
        json_t *root = json_loadf(file, 0, &error);
        fclose(file);
        if (!root)
        {
            printf("Failed to load settings: %s\n", error.text);
            return NULL;
        }
        return root;
    }
    else
    {
        printf("Failed to open settings file\n");
        return NULL;
    }
}

json_t *get_setting_value(const char *section, const char *setting)
{
    json_t *root = load_settings();
    if (!root)
    {
        return NULL;
    }

    json_t *section_obj = json_object_get(root, section);
    if (!section_obj)
    {
        printf("Section '%s' not found\n", section);
        json_decref(root);
        return NULL;
    }

    json_t *setting_obj = json_object_get(section_obj, setting);
    if (!setting_obj)
    {
        printf("Setting '%s' not found in section '%s'\n", setting, section);
        json_decref(root);
        return NULL;
    }

    json_t *value = json_object_get(setting_obj, "value");
    if (!value)
    {
        printf("Value not found for setting '%s' in section '%s'\n", setting, section);
        json_decref(root);
        return NULL;
    }

    // Increment the reference count before returning
    json_incref(value);

    // Release the root object
    json_decref(root);

    return value;
}

Setting* get_section_settings(const char* section, int* num_settings)
{
    json_t* root = load_settings();
    if (!root)
    {
        return NULL;
    }

    json_t* section_obj = json_object_get(root, section);
    if (!section_obj)
    {
        printf("Section '%s' not found\n", section);
        json_decref(root);
        return NULL;
    }

    size_t num_elements = json_object_size(section_obj);
    Setting* settings = (Setting*)malloc(num_elements * sizeof(Setting));
    if (!settings)
    {
        json_decref(root);
        return NULL;
    }

    int index = 0;
    const char* setting_name;
    json_t* setting_value;

    json_object_foreach(section_obj, setting_name, setting_value)
    {
        if (json_is_object(setting_value))
        {
            json_t* value = json_object_get(setting_value, "value");

            int setting_value_int = 0;

            if (json_is_integer(value))
            {
                setting_value_int = json_integer_value(value);
            }

            json_t* description = json_object_get(setting_value, "description");
            const char* setting_description = "";

            if (description)
            {
                char* json_description = json_dumps(description, JSON_ENCODE_ANY);
                if (json_description)
                {
                    setting_description = strdup(json_description);
                    free(json_description);
                }
            }

            settings[index].name = setting_name;
            settings[index].value = setting_value_int;
            settings[index].description = setting_description;

            index++;
        }
    }

    *num_settings = index;

    json_decref(root);

    return settings;
}