#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

#include <luajit.h>

#include "memory.h"
#include "process.h"

bool memory_error;
extern last_process process;

#define READ_MEMORY_FUNCTION(value_type) \
    value_type read_memory_##value_type(uint64_t mem_address) \
    { \
        value_type value; \
        \
        struct iovec mem_local; \
        struct iovec mem_remote; \
        \
        mem_local.iov_base = &value; \
        mem_local.iov_len = sizeof(value); \
        mem_remote.iov_len = sizeof(value); \
        mem_remote.iov_base = (void*)(uintptr_t)mem_address; \
        \
        ssize_t mem_n_read = process_vm_readv(process.pid, &mem_local, 1, &mem_remote, 1, 0); \
        if (mem_n_read == -1) \
        { \
            memory_error = true; \
        } \
        else if (mem_n_read != (ssize_t)mem_remote.iov_len) \
        { \
            printf("Error reading process memory: short read of %ld bytes\n", (long)mem_n_read); \
            exit(1); \
        } \
        \
        return value; \
    }

READ_MEMORY_FUNCTION(int8_t)
READ_MEMORY_FUNCTION(uint8_t)
READ_MEMORY_FUNCTION(int16_t)
READ_MEMORY_FUNCTION(uint16_t)
READ_MEMORY_FUNCTION(int32_t)
READ_MEMORY_FUNCTION(uint32_t)
READ_MEMORY_FUNCTION(int64_t)
READ_MEMORY_FUNCTION(uint64_t)
READ_MEMORY_FUNCTION(float)
READ_MEMORY_FUNCTION(double)
READ_MEMORY_FUNCTION(bool)

char* read_memory_string(uint64_t mem_address, int buffer_size)
{
    char* buffer = (char*)malloc(buffer_size);
    if (buffer == NULL)
    {
        // Handle memory allocation failure
        return NULL;
    }

    struct iovec mem_local;
    struct iovec mem_remote;

    mem_local.iov_base = buffer;
    mem_local.iov_len = buffer_size;
    mem_remote.iov_len = buffer_size;
    mem_remote.iov_base = (void*)(uintptr_t)mem_address;

    ssize_t mem_n_read = process_vm_readv(process.pid, &mem_local, 1, &mem_remote, 1, 0);
    if (mem_n_read == -1)
    {
        buffer[0] = '\0';
    }
    else if (mem_n_read != (ssize_t)mem_remote.iov_len)
    {
        printf("Error reading process memory: short read of %ld bytes\n", (long)mem_n_read);
        exit(1);
    }

    return buffer;
}

int read_address(lua_State* L)
{
    memory_error = false;
    uint64_t address;
    const char* value_type = lua_tostring(L, 1);
    int i;

    if (lua_isnumber(L, 2))
    {
        address = process.base_address + lua_tointeger(L, 2);
        i = 3;
    }
    else
    {
        const char* module = lua_tostring(L,2);
        if (strcmp(process.name, module) != 0)
        {
            process.dll_address = find_base_address(module);
        }
        address = process.dll_address + lua_tointeger(L, 3);
        i = 4;
    }

    for (; i <= lua_gettop(L); i++)
    {
        if (address <= UINT32_MAX)
        {
            address = read_memory_uint32_t((uint64_t)address);
        }
        else
        {
            address = read_memory_uint64_t(address);
        }
        address += lua_tointeger(L, i);
    }


    if (strcmp(value_type, "sbyte") == 0)
    {
        int8_t value = read_memory_int8_t(address);
        lua_pushinteger(L, (int)value);
    }
    else if (strcmp(value_type, "byte") == 0)
    {
        uint8_t value = read_memory_uint8_t(address);
        lua_pushinteger(L, (int)value);
    }
    else if (strcmp(value_type, "short") == 0)
    {
        short value = read_memory_int16_t(address);
        lua_pushinteger(L, (int)value);
    }
    else if (strcmp(value_type, "ushort") == 0)
    {
        unsigned short value = read_memory_uint16_t(address);
        lua_pushinteger(L, (int)value);
    }
    else if (strcmp(value_type, "int") == 0)
    {
        int value = read_memory_int32_t(address);
        lua_pushinteger(L, value);
    }
    else if (strcmp(value_type, "uint") == 0)
    {
        unsigned int value = read_memory_uint32_t(address);
        lua_pushinteger(L, (int)value);
    }
    else if (strcmp(value_type, "long") == 0)
    {
        long value = read_memory_int64_t(address);
        lua_pushinteger(L, (int)value);
    }
    else if (strcmp(value_type, "ulong") == 0)
    {
        unsigned long value = read_memory_uint64_t(address);
        lua_pushinteger(L, (int)value);
    }
    else if (strcmp(value_type, "float") == 0)
    {
        float value = read_memory_float(address);
        lua_pushnumber(L, (double)value);
    }
    else if (strcmp(value_type, "double") == 0)
    {
        double value = read_memory_double(address);
        lua_pushnumber(L, value);
    }
    else if (strcmp(value_type, "bool") == 0)
    {
        bool value = read_memory_bool(address);
        lua_pushboolean(L, value ? 1 : 0);
    }
    else if (strstr(value_type, "string") != NULL)
    {
        int buffer_size = atoi(value_type + 6);
        char* value = read_memory_string(address, buffer_size);
        lua_pushstring(L, value != NULL ? value : "");
        free(value);
        return 1;
    }
    else
    {
        printf("Invalid value type: %s\n", value_type);
        exit(1);
    }

    if (memory_error)
    {
        lua_pushinteger(L, -1);
    }

    return 1;
}
