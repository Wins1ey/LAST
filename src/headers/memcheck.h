#include <stdio.h>

extern int alloc_count;
extern int free_count;
extern FILE* logFile;
extern char logFilepath[256];

typedef struct
{
    void* pointer;
    size_t* size;
} MemoryBlock;

extern MemoryBlock* allocated_blocks;

void* tracked_malloc(size_t size);
void tracked_free(void* ptr);