#include "headers/memcheck.h"

int alloc_count = 0;
int free_count = 0;
char logFilepath[256];
FILE* logFile;

void* tracked_malloc(size_t size)
{
    void* ptr = malloc(size);
    if(ptr != NULL)
        alloc_count++;

    return ptr;
}

void tracked_free(void* ptr)
{
    if(ptr != NULL)
    {
        free(ptr);
        free_count++;
    }
}