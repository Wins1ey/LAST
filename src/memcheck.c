#include "headers/memcheck.h"

int alloc_count = 0;
int free_count = 0;
char logFilepath[256];
FILE* logFile;
