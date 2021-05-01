#ifndef _LIBRDMAPMEM_H_
#define _LIBRDMAPMEM_H_

#include <libpmem.h>
#include <libpmemobj.h>
#include <libpmempool.h>

#include <sys/wait.h>
#include <sys/types.h>
#include <getopt.h>

#include "Client.hpp"
Client* client;
struct Config
{
    uint32_t sock_port_ = 5678;
    uint32_t ib_port_ = 1;
    std::string ib_dev_;
    std::string config_file_;
} config;

bool ConnectServer(int argc, char** argv); //

void DisConnectServer();

PMEMobjpool* rdmapmemobj_open(const char* path, const char* layout);

PMEMobjpool* rdmapmemobj_create(const char* path, const char* layout,
                                size_t poolsize, mode_t mode);

void rdmapmemobj_close(PMEMobjpool* pop);

// read oid ptr's data
void rdmapmem_direct_read(PMEMoid oid, size_t size, void* result);

// write oid ptr's data
void rdmapmem_direct_write(PMEMoid oid, size_t size, void* source);

#endif // !_PMEM_H
