#ifndef __AUTO_SPLITTER_H__
#define __AUTO_SPLITTER_H__

#include <stdatomic.h>

extern atomic_bool auto_splitter_enabled;
extern atomic_bool call_start;
extern atomic_bool call_split;
extern atomic_bool toggle_loading;
extern atomic_bool call_reset;
extern char auto_splitter_file[256];

void run_auto_splitter();

#endif /* __AUTO_SPLITTER_H__ */