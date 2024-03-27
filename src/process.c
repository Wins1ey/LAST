#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <luajit.h>

#include "process.h"
#include "auto-splitter.h"

struct last_process process;

void execute_command(const char* command, char* output)
{
    char buffer[4096];
    FILE* pipe = popen(command, "r");
    if (!pipe)
    {
        fprintf(stderr, "Error executing command: %s\n", command);
        exit(1);
    }

    while (fgets(buffer, 128, pipe) != NULL)
    {
        strcat(output, buffer);
    }

    pclose(pipe);
}

/*
Gets the base address of a module
if `module` equals to a nullptr, the main process is used, else it will search for the base addr of the specified module
*/
uintptr_t find_base_address(const char* module)
{
    const char* module_to_grep = module == 0 ? process.name : module;

    char path[22]; // 22 is the maximum length the path can be (strlen("/proc/4294967296/maps"))

    snprintf(path, sizeof(path), "/proc/%d/maps", process.pid);

    FILE *f = fopen(path, "r");

    if (f) {
        char current_line[1024];
        while (fgets(current_line, sizeof(current_line), f) != NULL) {
            if (strstr(current_line, module_to_grep) == NULL)
                continue;
            fclose(f);
            size_t dash_pos = strcspn(current_line, "-");

            if (dash_pos != strlen(current_line)) {
                char first_number[32];
                strncpy(first_number, current_line, dash_pos);
                first_number[dash_pos] = '\0';
                uintptr_t addr = strtoull(first_number, NULL, 16);
                return addr;
                break;
            }
        }
        fclose(f);
    }
    printf("Couldn't find base address\n");
    return 0;
}

void stock_process_id(const char* pid_command)
{
    char pid_output[4096];
    pid_output[0] = '\0';

    while (atomic_load(&auto_splitter_enabled))
    {
        execute_command(pid_command, pid_output);
        process.pid = strtoul(pid_output, NULL, 10);
        printf("\033[2J\033[1;1H"); // Clear the console
        if (process.pid) {
            size_t newlinePos = strcspn(pid_output, "\n");
            if (newlinePos != strlen(pid_output) - 1 && pid_output[0] != '\0')
            {
                printf("Multiple PID's found for process: %s\n", process.name);
            }
            break;
        } else {
            printf("%s isn't running.\n", process.name);
            usleep(100000); // Sleep for 100ms
        }
    }

    printf("Process: %s\n", process.name);
    printf("PID: %u\n", process.pid);
    process.base_address = find_base_address(NULL);
    process.dll_address = process.base_address;
}

int find_process_id(lua_State* L)
{
    process.name = lua_tostring(L, 1);
    char command[256];
    printf("\033[2J\033[1;1H"); // Clear the console
    snprintf(command, sizeof(command), "pgrep \"%.*s\"", (int)strnlen(process.name, 15), process.name);

    stock_process_id(command);

    return 0;
}

int getPid(lua_State* L) {
    lua_pushinteger(L, process.pid);
    return 1;
}

int process_exists()
{
    int result = kill(process.pid, 0);
    return result == 0;
}
