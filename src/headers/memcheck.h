#include <stdio.h>

extern int alloc_count;
extern int free_count;
extern FILE* logFile;
extern char logFilepath[256];

void* tracked_malloc(size_t size);
void tracked_free(void* ptr);