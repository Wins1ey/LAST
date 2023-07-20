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

void lua_startup(lua_State* L)
{
    lua_getglobal(L, "Startup");
    lua_pcall(L, 0, 0, 0);

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
    lua_pop(L, 1); // Remove 'Process' from the stack

    lua_getglobal(L, "RefreshRate");
    if (lua_isnumber(L, -1))
    {
        refresh_rate = lua_tointeger(L, -1);
    }
    lua_pop(L, 1); // Remove 'RefreshRate' from the stack
}

void lua_init(lua_State* L)
{
    lua_getglobal(L, "Init");
    lua_pcall(L, 0, 0, 0);
}

void lua_state(lua_State* L)
{
    lua_getglobal(L, "State");
    lua_pcall(L, 0, 0, 0);
}

void lua_update(lua_State* L)
{
    lua_getglobal(L, "Update");
    lua_pcall(L, 0, 0, 0);
}

void lua_start(lua_State* L)
{
    lua_getglobal(L, "Start");
    lua_pcall(L, 0, 1, 0);
    if (lua_toboolean(L, -1))
    {
        atomic_store(&call_start, true);
    }
    lua_pop(L, 1); // Remove the return value from the stack
}

void lua_split(lua_State* L)
{
    lua_getglobal(L, "Split");
    lua_pcall(L, 0, 1, 0);
    if (lua_toboolean(L, -1))
    {
        atomic_store(&call_split, true);
    }
    lua_pop(L, 1); // Remove the return value from the stack
}

void lua_is_loading(lua_State* L)
{
    lua_getglobal(L, "IsLoading");
    lua_pcall(L, 0, 1, 0);
    if (lua_toboolean(L, -1) != prev_is_loading)
    {
        atomic_store(&toggle_loading, true);
        prev_is_loading = !prev_is_loading;
    }
    lua_pop(L, 1); // Remove the return value from the stack
}

void lua_reset(lua_State* L)
{
    lua_getglobal(L, "Reset");
    lua_pcall(L, 0, 1, 0);
    if (lua_toboolean(L, -1))
    {
        atomic_store(&call_reset, true);
    }
    lua_pop(L, 1); // Remove the return value from the stack
}

void setup_sandbox(lua_State* L)
{
    luaL_openlibs(L);

    lua_newtable(L);

    lua_pushnil(L);
    lua_setglobal(L, "os"); // Disable 'os' library
    lua_pushnil(L);
    lua_setglobal(L, "io"); // Disable 'io' library
    lua_pushnil(L);
    lua_setglobal(L, "debug"); // Disable 'debug' library
    lua_pushnil(L);
    lua_setglobal(L, "package"); // Disable 'package' library
    
    
    lua_pushnil(L);
    lua_setglobal(L, "dofile"); // Disable 'dofile' function
    lua_pushnil(L);
    lua_setglobal(L, "loadfile"); // Disable 'loadfile' function
    lua_pushnil(L);
    lua_setglobal(L, "load"); // Disable 'load' function
    lua_pushnil(L);
    lua_setglobal(L, "require"); // Disable 'require' function

    lua_getglobal(L, "string");
    lua_pushnil(L);
    lua_setfield(L, -2, "dump"); // Disable 'string.dump' function
    lua_pop(L, 1); // Pop the 'string' table from the stack

    lua_pushcfunction(L, read_address);
    lua_setglobal(L, "ReadAddress");

    lua_setglobal(L, "_G");
}

void run_auto_splitter()
{
    char current_file[MAX_PATH_LENGTH];
    strcpy(current_file, auto_splitter_file);

    lua_State* L = luaL_newstate();
    setup_sandbox(L);

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

    lua_getglobal(L, "Init");
    bool init_exists = lua_isfunction(L, -1);
    lua_pop(L, 1); // Remove 'Init' from the stack

    lua_getglobal(L, "State");
    bool state_exists = lua_isfunction(L, -1);
    lua_pop(L, 1); // Remove 'state' from the stack

    lua_getglobal(L, "Start");
    bool start_exists = lua_isfunction(L, -1);
    lua_pop(L, 1); // Remove 'start' from the stack

    lua_getglobal(L, "Split");
    bool split_exists = lua_isfunction(L, -1);
    lua_pop(L, 1); // Remove 'split' from the stack

    lua_getglobal(L, "IsLoading");
    bool is_loading_exists = lua_isfunction(L, -1);
    lua_pop(L, 1); // Remove 'isLoading' from the stack

    lua_getglobal(L, "Startup");
    bool startup_exists = lua_isfunction(L, -1);
    lua_pop(L, 1); // Remove 'Startup' from the stack

    lua_getglobal(L, "Reset");
    bool reset_exists = lua_isfunction(L, -1);
    lua_pop(L, 1); // Remove 'reset' from the stack

    lua_getglobal(L, "Update");
    bool update_exists = lua_isfunction(L, -1);
    lua_pop(L, 1); // Remove 'update' from the stack

    if (startup_exists)
    {
        lua_startup(L);
    }

    if (init_exists)
    {
        lua_init(L);
    }

    if (state_exists)
    {
        lua_state(L);
    }

    printf("Refresh rate: %d\n", refresh_rate);
    int rate = 1000000 / refresh_rate;

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
            if (init_exists)
            {
                lua_init(L);
            }
        }

        if (state_exists)
        {
            lua_state(L);
        }

        if (update_exists)
        {
            lua_update(L);
        }

        if (start_exists)
        {
            lua_start(L);
        }

        if (split_exists)
        {
            lua_split(L);
        }

        if (is_loading_exists)
        {
            lua_is_loading(L);
        }

        if (reset_exists)
        {
            lua_reset(L);
        }

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
    while (true)
    {
        if (atomic_load(&auto_splitter_enabled) && auto_splitter_file[0] != '\0')
        {
            run_auto_splitter();
        }
        usleep(10000); // Wait for 10 milliseconds before checking again
    }
}