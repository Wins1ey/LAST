#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <pwd.h>
#include <sys/stat.h>
#include <string.h>

#include <luajit.h>
#include <lualib.h>
#include <lauxlib.h>

#include "auto-splitter.h"
#include "lib_init.h"
#include "memory.h"
#include "process.h"

char auto_splitter_file[256];
int refresh_rate = 60;
atomic_bool auto_splitter_enabled = true;
atomic_bool call_start = false;
atomic_bool call_split = false;
atomic_bool toggle_loading = false;
atomic_bool call_reset = false;
bool prev_is_loading;

extern last_process process;

void lua_disablefunction(lua_State* L, const char* module_name, const char* func_name)
{
    if (module_name == NULL)
    {
        lua_pushnil(L);
        lua_setglobal(L, func_name);
    }
    else
    {
        lua_getglobal(L, module_name);
        lua_pushnil(L);
        lua_setfield(L, -2, func_name);
        lua_pop(L, 1);
    }
}

void lua_disablefunctions(lua_State* L, const char* module_name, const char* func_names[])
{
    for (int i = 0; func_names[i] != NULL; i++)
    {
        lua_disablefunction(L, module_name, func_names[i]);
    }
}

void lua_sandbox(lua_State* L)
{
    const char* main_funcs[] = {
        "collectgarbage",
        "dofile",
        "getmetatable",
        "setmetatable",
        "getfenv",
        "setfenv",
        "load",
        "loadfile",
        "loadstring",
        "rawequal",
        "rawget",
        "rawset",
        "module",
        "require",
        "newproxy",
        NULL
    };
    const char* os_funcs[] = {
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

    lua_disablefunctions(L, NULL, main_funcs);
    lua_disablefunctions(L, "os", os_funcs);
    lua_disablefunction(L, "string", "dump");
    lua_disablefunction(L, "math", "randomseed");
    
    lua_pushcfunction(L, read_address);
    lua_setglobal(L, "readAddress");

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

int lua_startup(lua_State* L, char* current_file)
{
    if (lua_callfunction(L, "startup", 0, 0))
    {
        lua_getglobal(L, "refreshRate");
        if (lua_isnumber(L, -1))
        {
            refresh_rate = lua_tointeger(L, -1);
        }
        lua_pop(L, 1);

        lua_getglobal(L, "process");
        if (lua_isstring(L, -1))
        {
            const char* process_name = lua_tostring(L, -1);
            if (process_name != NULL)
            {
                return find_process_id(process_name, current_file);
            }
        }
        else
        {
            printf("process isn't defined as a string\n");
            atomic_store(&auto_splitter_enabled, false);
        }
        lua_pop(L, 1);
    }
    return 0;
}

void lua_init(lua_State* L)
{
    lua_callfunction(L, "init", 0, 0);
}

void lua_state(lua_State* L)
{
    lua_callfunction(L, "state", 0, 0);
}

int lua_update(lua_State* L)
{
    if(lua_callfunction(L, "update", 0, 1))
    {
        if (lua_isboolean(L, -1))
        {
            return lua_toboolean(L, -1);
        }
        lua_pop(L, 1);
    }
    return 1;
}

void lua_onstart(lua_State* L)
{
    lua_callfunction(L, "onStart", 0, 0);
}

void lua_start(lua_State* L)
{
    if (lua_callfunction(L, "start", 0, 1))
    {
        if (lua_toboolean(L, -1))
        {
            atomic_store(&call_start, true);
            lua_onstart(L);
        }
        lua_pop(L, 1);
    }
}

void lua_onsplit(lua_State* L)
{
    lua_callfunction(L, "onSplit", 0, 0);
}

void lua_split(lua_State* L)
{
    if (lua_callfunction(L, "split", 0, 1))
    {       
        if (lua_toboolean(L, -1))
        {
            atomic_store(&call_split, true);
            lua_onsplit(L);
        }
        lua_pop(L, 1);
    }
}

void lua_isloading(lua_State* L)
{
    if (lua_callfunction(L, "isLoading", 0, 1))
    {
        if (lua_toboolean(L, -1) != prev_is_loading)
        {
            atomic_store(&toggle_loading, true);
            prev_is_loading = !prev_is_loading;
        }
        lua_pop(L, 1);
    }
}

void lua_onreset(lua_State* L)
{
    lua_callfunction(L, "onReset", 0, 0);
}

int lua_reset(lua_State* L)
{
    if (lua_callfunction(L, "reset", 0, 1))
    {
        if (lua_toboolean(L, -1))
        {
            atomic_store(&call_reset, true);
            lua_onreset(L);
            return 1;
        }
        lua_pop(L, 1);
    }
    return 0;
}

void lua_exit(lua_State* L)
{
    lua_callfunction(L, "exit", 0, 0);
}

void lua_shutdown(lua_State* L)
{
    lua_callfunction(L, "shutdown", 0, 0);
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

    char current_file[256];
    strcpy(current_file, auto_splitter_file);

    if (lua_startup(L, current_file))
    {
        lua_init(L);
    }

    int rate = 1000000 / refresh_rate;

    while (1)
    {
        struct timespec clock_start;
        clock_gettime(CLOCK_MONOTONIC, &clock_start);

        if (!atomic_load(&auto_splitter_enabled) || strcmp(current_file, auto_splitter_file) != 0)
        {
            lua_shutdown(L);
            break;
        }
        if (!process_exists())
        {
            lua_exit(L);
            if (find_process_id(process.name, current_file))
            {
                lua_init(L);
            }
        }

        lua_state(L);
        if (lua_update(L) != 0)
        {
            lua_isloading(L);
            if (lua_reset(L) != 1)
            {
                lua_split(L);
            }
            lua_start(L);
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