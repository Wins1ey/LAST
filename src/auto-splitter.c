#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <pwd.h>
#include <sys/stat.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "memory.h"
#include "auto-splitter.h"
#include "process.h"

#define MAX_PATH_LENGTH 256

char auto_splitter_file[MAX_PATH_LENGTH];
int refresh_rate = 60;
atomic_bool auto_splitter_enabled = true;
atomic_bool call_start = false;
atomic_bool call_split = false;
atomic_bool toggle_loading = false;
atomic_bool call_reset = false;
bool prev_is_loading;

extern last_process process;

void check_directories()
{
    // Get the path to the user's directory
    struct passwd *pw = getpwuid(getuid());
    const char *user_directory = pw->pw_dir;
    char last_directory[241];
    char auto_splitters_directory[MAX_PATH_LENGTH];
    char themes_directory[MAX_PATH_LENGTH];
    char splits_directory[MAX_PATH_LENGTH];
    snprintf(last_directory, MAX_PATH_LENGTH, "%s/.last", user_directory);
    snprintf(auto_splitters_directory, MAX_PATH_LENGTH, "%s/auto-splitters", last_directory);
    snprintf(themes_directory, MAX_PATH_LENGTH, "%s/themes", last_directory);
    snprintf(splits_directory, MAX_PATH_LENGTH, "%s/splits", last_directory);

    // Make the LAST directory if it doesn't exist
    if (mkdir(last_directory, 0755) == -1) {
        // Directory already exists or there was an error
    }

    // Make the autosplitters directory if it doesn't exist
    if (mkdir(auto_splitters_directory, 0755) == -1) {
        // Directory already exists or there was an error
    }

    // Make the themes directory if it doesn't exist
    if (mkdir(themes_directory, 0755) == -1) {
        // Directory already exists or there was an error
    }

    // Make the splits directory if it doesn't exist
    if (mkdir(splits_directory, 0755) == -1) {
        // Directory already exists or there was an error
    }
}

static int lua_disable(lua_State* L) {
    const char* func_name = lua_tostring(L, lua_upvalueindex(1));
    printf("'%s' has been disabled.\n", func_name);
    return 0;
}

void lua_disablefunction(lua_State* L, const char* module_name, const char* func_name)
{
    if (module_name == NULL)
    {
        lua_pushstring(L, func_name);
        lua_pushcclosure(L, lua_disable, 1);
        lua_setglobal(L, func_name);
    }
    else
    {
        lua_getglobal(L, module_name);
        char combined[256]; // Adjust the buffer size as needed
        snprintf(combined, sizeof(combined), "%s.%s", module_name, func_name);
        lua_pushstring(L, combined);
        lua_pushcclosure(L, lua_disable, 1);
        lua_setfield(L, -2, func_name);
        lua_pop(L, 1);
    }
}

void lua_disablefunctions(lua_State* L, const char* module_name, const char* func_names[])
{
    if (module_name == NULL)
    {
        for (int i = 0; func_names[i] != NULL; i++)
        {
            lua_pushstring(L, func_names[i]);
            lua_pushcclosure(L, lua_disable, 1);
            lua_setglobal(L, func_names[i]);
        }
    }
    else
    {
        lua_getglobal(L, module_name);
        for (int i = 0; func_names[i] != NULL; i++)
        {
            char combined[256]; // Adjust the buffer size as needed
            snprintf(combined, sizeof(combined), "%s.%s", module_name, func_names[i]);
            lua_pushstring(L, combined);
            lua_pushcclosure(L, lua_disable, 1);
            lua_setfield(L, -2, func_names[i]);
        }
        lua_pop(L, 1);
    }
}

void lua_disablemodule(lua_State* L, const char* module_name)
{
    lua_pushstring(L, module_name);
    lua_pushcclosure(L, lua_disable, 1);
    lua_setglobal(L, module_name);
}

void lua_sandbox(lua_State* L)
{
    const char* main_functions[] = {
        "collectgarbage",
        "dofile",
        "getmetatable",
        "setmetatable",
        "load",
        "loadfile",
        "rawequal",
        "rawget",
        "rawlen",
        "rawset",
        "require",
        NULL
    };
    const char* os_functions[] = {
        "execute",
        "exit",
        "getenv",
        "remove",
        "rename",
        "setlocale",
        "tmpname",
        NULL
    };
    luaL_openlibs(L);

    lua_newtable(L);

    lua_disablemodule(L, "io");
    lua_disablemodule(L, "debug");
    lua_disablemodule(L, "package");
    lua_disablefunction(L, "string", "dump");
    lua_disablefunctions(L, NULL, main_functions);
    lua_disablefunctions(L, "os", os_functions);
    
    lua_pushcfunction(L, read_address);
    lua_setglobal(L, "ReadAddress");

    lua_setglobal(L, "_G");
}
int lua_callfunction(lua_State* L, const char* func_name, int num_args, int num_returns)
{
    lua_getglobal(L, func_name);
    if (lua_isfunction(L, -1))
    {
        if (lua_pcall(L, num_args, num_returns, 0) != LUA_OK)
        {
            printf("Error executing Lua function '%s': %s\n", func_name, lua_tostring(L, -1));
            return 0;
        }
        return 1;
    }
    return 0;
}

void lua_startup(lua_State* L)
{
    if (lua_callfunction(L, "Startup", 0, 0))
    {
        lua_getglobal(L, "Process");
        if (lua_isstring(L, -1))
        {
            const char* process_name = lua_tostring(L, -1);
            if (process_name != NULL)
            {
                find_process_id(process_name);
            }
        }
        else
        {
            printf("Process isn't defined as a string\n");
            atomic_store(&auto_splitter_enabled, false);
        }
        lua_pop(L, 1);

        lua_getglobal(L, "RefreshRate");
        if (lua_isnumber(L, -1))
        {
            refresh_rate = lua_tointeger(L, -1);
        }
        lua_pop(L, 1);
    }
}

void lua_init(lua_State* L)
{
    lua_callfunction(L, "Init", 0, 0);
}

void lua_state(lua_State* L)
{
    lua_callfunction(L, "State", 0, 0);
}

void lua_update(lua_State* L)
{
    lua_callfunction(L, "Update", 0, 0);
}

void lua_start(lua_State* L)
{
    if (lua_callfunction(L, "Start", 0, 1))
    {
        if (lua_toboolean(L, -1))
        {
            atomic_store(&call_start, true);
        }
        lua_pop(L, 1);
    }
}

void lua_split(lua_State* L)
{
    if (lua_callfunction(L, "Split", 0, 1))
    {       
        if (lua_toboolean(L, -1))
        {
            atomic_store(&call_split, true);
        }
        lua_pop(L, 1);
    }
}

void lua_is_loading(lua_State* L)
{
    if (lua_callfunction(L, "IsLoading", 0, 1))
    {
        if (lua_toboolean(L, -1) != prev_is_loading)
        {
            atomic_store(&toggle_loading, true);
            prev_is_loading = !prev_is_loading;
        }
        lua_pop(L, 1);
    }
}

void lua_reset(lua_State* L)
{
    if (lua_callfunction(L, "Reset", 0, 1))
    {
        if (lua_toboolean(L, -1))
        {
            atomic_store(&call_reset, true);
        }
        lua_pop(L, 1);
    }
}

void run_auto_splitter()
{
    lua_State* L = luaL_newstate();

    // Load the Lua file
    if (luaL_loadfile(L, auto_splitter_file) != LUA_OK)
    {
        fprintf(stderr, "Lua syntax error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        atomic_store(&auto_splitter_enabled, false);
        return;
    }

    // Run the Lua file
    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
        printf("Error executing Lua file: %s\n", lua_tostring(L, -1));
        lua_close(L);
        atomic_store(&auto_splitter_enabled, false);
        return;
    }
    lua_sandbox(L);

    lua_startup(L);
    lua_init(L);
    lua_state(L);

    printf("Refresh rate: %d\n", refresh_rate);
    int rate = 1000000 / refresh_rate;

    char current_file[MAX_PATH_LENGTH];
    strcpy(current_file, auto_splitter_file);

    while (1)
    {
        struct timespec clock_start;
        clock_gettime(CLOCK_MONOTONIC, &clock_start);

        if (!atomic_load(&auto_splitter_enabled) || strcmp(current_file, auto_splitter_file) != 0)
        {
            break;
        }
        if (!process_exists())
        {
            find_process_id(process.name);
            lua_init(L);
        }

        lua_state(L);
        lua_update(L);
        lua_start(L);
        lua_split(L);
        lua_is_loading(L);
        lua_reset(L);

        struct timespec clock_end;
        clock_gettime(CLOCK_MONOTONIC, &clock_end);
        long long duration = (clock_end.tv_sec - clock_start.tv_sec) * 1000000 + (clock_end.tv_nsec - clock_start.tv_nsec) / 1000;
        if (duration < rate)
        {
            usleep(rate - duration);
        }
    }
    lua_close(L);
}

void *last_auto_splitter()
{
    while (1)
    {
        if (atomic_load(&auto_splitter_enabled) && auto_splitter_file[0] != '\0')
        {
            run_auto_splitter();
        }
        usleep(10000); // Wait for 10 milliseconds before checking again
    }
}