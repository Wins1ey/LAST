#ifndef __MEMORY_H__
#define __MEMORY_H__

#include <lua.h>
#include <sys/uio.h>
#include <stdlib.h>

ssize_t process_vm_readv(int pid, struct iovec *mem_local, int liovcnt, struct iovec *mem_remote, int riovcnt, int flags);

int read_address(lua_State* L);

#endif /* __MEMORY_H__ */