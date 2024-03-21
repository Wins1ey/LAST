#include <linux/limits.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <pwd.h>
#include <sys/stat.h>
#include <string.h>

#include <luajit.h>
#include <lualib.h>
#include <lauxlib.h>

#include "memory.h"
#include "auto-splitter.h"
#include "process.h"
#include "settings.h"

char auto_splitter_file[PATH_MAX];
int refresh_rate = 60;
atomic_bool auto_splitter_enabled = true;
atomic_bool call_start = false;
atomic_bool call_split = false;
atomic_bool toggle_loading = false;
atomic_bool call_reset = false;
bool prev_is_loading;

extern last_process process;

// I have no idea how this works
// https://stackoverflow.com/a/2336245
static void mkdir_p(const char *dir, __mode_t permissions) {
    char tmp[256] = {0};
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp),"%s",dir);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, permissions);
            *p = '/';
        }
    mkdir(tmp, permissions);
}

void check_directories()
{
    char last_directory[PATH_MAX] = {0};
    get_LAST_folder_path(last_directory);

    char auto_splitters_directory[PATH_MAX];
    char themes_directory[PATH_MAX];
    char splits_directory[PATH_MAX];

    strcpy(auto_splitters_directory, last_directory);
    strcat(auto_splitters_directory, "/auto-splitters");

    strcpy(themes_directory, last_directory);
    strcat(themes_directory, "/themes");

    strcpy(splits_directory, last_directory);
    strcat(splits_directory, "/splits");

    // Make the LAST directory if it doesn't exist
    mkdir_p(last_directory, 0755);

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

void startup(lua_State* L)
{
    lua_getglobal(L, "startup");
    lua_pcall(L, 0, 0, 0);

    lua_getglobal(L, "refreshRate");
    if (lua_isnumber(L, -1))
    {
        refresh_rate = lua_tointeger(L, -1);
    }
    lua_pop(L, 1); // Remove 'refreshRate' from the stack
}

void state(lua_State* L)
{
    lua_getglobal(L, "state");
    lua_pcall(L, 0, 0, 0);
}

void update(lua_State* L)
{
    lua_getglobal(L, "update");
    lua_pcall(L, 0, 0, 0);
}

void start(lua_State* L)
{
    lua_getglobal(L, "start");
    lua_pcall(L, 0, 1, 0);
    if (lua_toboolean(L, -1))
    {
        atomic_store(&call_start, true);
    }
    lua_pop(L, 1); // Remove the return value from the stack
}

void split(lua_State* L)
{
    lua_getglobal(L, "split");
    lua_pcall(L, 0, 1, 0);
    if (lua_toboolean(L, -1))
    {
        atomic_store(&call_split, true);
    }
    lua_pop(L, 1); // Remove the return value from the stack
}

void is_loading(lua_State* L)
{
    lua_getglobal(L, "isLoading");
    lua_pcall(L, 0, 1, 0);
    if (lua_toboolean(L, -1) != prev_is_loading)
    {
        atomic_store(&toggle_loading, true);
        prev_is_loading = !prev_is_loading;
    }
    lua_pop(L, 1); // Remove the return value from the stack
}

void reset(lua_State* L)
{
    lua_getglobal(L, "reset");
    lua_pcall(L, 0, 1, 0);
    if (lua_toboolean(L, -1))
    {
        atomic_store(&call_reset, true);
    }
    lua_pop(L, 1); // Remove the return value from the stack
}

void run_auto_splitter()
{
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushcfunction(L, find_process_id);
    lua_setglobal(L, "process");
    lua_pushcfunction(L, read_address);
    lua_setglobal(L, "readAddress");
    lua_pushcfunction(L, getPid);
    lua_setglobal(L, "getPID");

    char current_file[PATH_MAX];
    strcpy(current_file, auto_splitter_file);

    // Load the Lua file
    if (luaL_loadfile(L, auto_splitter_file) != LUA_OK)
    {
        // Error loading the file
        const char* error_msg = lua_tostring(L, -1);
        lua_pop(L, 1); // Remove the error message from the stack
        fprintf(stderr, "Lua syntax error: %s\n", error_msg);
        lua_close(L);
        atomic_store(&auto_splitter_enabled, false);
        return;
    }

    // Execute the Lua file
    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
    {
        // Error executing the file
        const char* error_msg = lua_tostring(L, -1);
        lua_pop(L, 1); // Remove the error message from the stack
        fprintf(stderr, "Lua runtime error: %s\n", error_msg);
        lua_close(L);
        atomic_store(&auto_splitter_enabled, false);
        return;
    }

    lua_getglobal(L, "state");
    bool state_exists = lua_isfunction(L, -1);
    lua_pop(L, 1); // Remove 'state' from the stack

    lua_getglobal(L, "start");
    bool start_exists = lua_isfunction(L, -1);
    lua_pop(L, 1); // Remove 'start' from the stack

    lua_getglobal(L, "split");
    bool split_exists = lua_isfunction(L, -1);
    lua_pop(L, 1); // Remove 'split' from the stack

    lua_getglobal(L, "isLoading");
    bool is_loading_exists = lua_isfunction(L, -1);
    lua_pop(L, 1); // Remove 'isLoading' from the stack

    lua_getglobal(L, "startup");
    bool startup_exists = lua_isfunction(L, -1);
    lua_pop(L, 1); // Remove 'startup' from the stack

    lua_getglobal(L, "reset");
    bool reset_exists = lua_isfunction(L, -1);
    lua_pop(L, 1); // Remove 'reset' from the stack

    lua_getglobal(L, "update");
    bool update_exists = lua_isfunction(L, -1);
    lua_pop(L, 1); // Remove 'update' from the stack

    if (startup_exists)
    {
        startup(L);
    }

    if (state_exists)
    {
        state(L);
    }

    printf("Refresh rate: %d\n", refresh_rate);
    int rate = 1000000 / refresh_rate;

    while (1)
    {
        struct timespec clock_start;
        clock_gettime(CLOCK_MONOTONIC, &clock_start);

        if (!atomic_load(&auto_splitter_enabled) || strcmp(current_file, auto_splitter_file) != 0 || !process_exists() || process.pid == 0)
        {
            break;
        }

        if (state_exists)
        {
            state(L);
        }

        if (update_exists)
        {
            update(L);
        }

        if (start_exists)
        {
            start(L);
        }

        if (split_exists)
        {
            split(L);
        }

        if (is_loading_exists)
        {
            is_loading(L);
        }

        if (reset_exists)
        {
            reset(L);
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
