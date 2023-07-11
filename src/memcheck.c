#include "headers/memcheck.h"
#include <stdlib.h>

int alloc_count = 0;
int free_count = 0;
char logFilepath[256];
FILE* logFile;
MemoryBlock* allocated_blocks = NULL;


void* tracked_malloc(size_t size)
{
    void* ptr = malloc(size);
    if(ptr != NULL)
    {
        MemoryBlock* new_block = realloc(allocated_blocks, (alloc_count + 1) * sizeof(MemoryBlock));
        allocated_blocks = new_block;
        allocated_blocks[alloc_count].pointer = ptr;
        allocated_blocks[alloc_count].size = size;
        alloc_count++;
    }
    return ptr;
}

void tracked_free(void* ptr)
{
    if(ptr != NULL)
    {
        int index = -1;
        for(int i = 0; i < alloc_count; i++)
        {
            if(allocated_blocks[i].pointer == ptr)
            {
                index = i;
                break;
            }
        }
        if(index >= 0)
        {
            for(int i = index; i < alloc_count - 1; i++)
            {
                allocated_blocks[i] = allocated_blocks[i + 1];
            }
            allocated_blocks--;
            free_count++;
            MemoryBlock* new_block = realloc(allocated_blocks, alloc_count * sizeof(MemoryBlock));
            if(alloc_count > 0 && new_block == NULL)
            {
                printf("Memory reallocation failed.\n");
                return;
            }
            allocated_blocks = new_block;
        }
    }
}