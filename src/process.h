#ifndef __PROCESS_H__
#define __PROCESS_H__

#include <stdint.h>

#include <luajit.h>

struct last_process
{
    const char *name;
    int pid;
    uintptr_t base_address;
    uintptr_t dll_address;
};
typedef struct last_process last_process;

uintptr_t find_base_address(const char* module);
int process_exists();
int find_process_id(lua_State* L);
int getPid(lua_State* L);

#endif /* __PROCESS_H__ */
