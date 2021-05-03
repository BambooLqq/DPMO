#include "librdmapmem.hpp"

static struct option long_options[5]
    = {{.name = "port", .has_arg = 1, .flag = 0, .val = 'p'},
       {.name = "ib-dev", .has_arg = 1, .flag = 0, .val = 'd'},
       {.name = "ib-port", .has_arg = 1, .flag = 0, .val = 'i'},
       {.name = "config-file", .has_arg = 1, .flag = 0, .val = 'c'},
       {.name = NULL, .has_arg = 0, .flag = 0, .val = '\0'}};

static void usage(const char* argv0)
{
    fprintf(stdout, "Usage:\n");
    fprintf(stdout, " %s start a server and wait for connection\n", argv0);
    fprintf(stdout, " %s <host> connect to server at <host>\n", argv0);
    fprintf(stdout, "\n");
    fprintf(stdout, "Options:\n");
    fprintf(
        stdout,
        " -p, --port <port> listen on/connect to port <port> (default 0)\n");
    fprintf(
        stdout,
        " -d, --ib-dev <dev> use IB device <dev> (default first device found)\n");
    fprintf(stdout,
            " -i, --ib-port <port> use port <port> of IB device (default 1)\n");
    fprintf(stdout, " -c, --config-file <config-file> config-file path\n");
}

static void print_config(void)
{
    fprintf(stdout, " ------------------------------------------------\n");
    fprintf(stdout, " Device name : \"%s\"\n", config.ib_dev_.c_str());
    fprintf(stdout, " IB port : %u\n", config.ib_port_);
    fprintf(stdout, " TCP port : %u\n", config.sock_port_);
    if (config.config_file_.size() > 0)
        fprintf(stdout, " Config_file: %s\n", config.config_file_.c_str());
    fprintf(stdout, " ------------------------------------------------\n\n");
}

static int ParseArgv(int argc, char** argv)
{
    while (1)
    {
        int c;
        c = getopt_long(argc, argv, "p:d:i:c:", long_options, NULL);
        if (c == -1) break;
        switch (c)
        {
        case 'p': config.sock_port_ = strtoul(optarg, NULL, 0); break;
        case 'd': config.ib_dev_ = strdup(optarg); break;
        case 'i':
            config.ib_port_ = strtoul(optarg, NULL, 0);
            if (config.ib_port_ < 0)
            {
                usage(argv[0]);
                return 1;
            }
            break;
        case 'c':
            if (optarg == NULL)
            {
                usage(argv[0]);
                return 1;
            }
            else
            {
                config.config_file_ = optarg;
            }
            break;
        default: usage(argv[0]); return 1;
        }
    }
    if (optind < argc)
    {
        usage(argv[0]);
        return 1;
    }
    return 0;
}

bool ConnectServer(int argc, char** argv)
{
    if (ParseArgv(argc, argv))
    {
        return false;
    }
    print_config();
    client = new Client(config.sock_port_, config.config_file_, config.ib_dev_,
                        config.ib_port_);
    if (client != NULL)
    {
        return true;
    }
    else
    {
        return false;
    }
}

uint64_t rdmapmemobj_open(uint16_t node_id, const char* path,
                          const char* layout)
{
    return client->OpenRemotePool(node_id, path, layout);
}

uint64_t rdmapmemobj_create(uint16_t node_id, const char* path,
                            const char* layout, size_t poolsize, mode_t mode)
{
    return client->CreateRemotePool(node_id, path, layout, poolsize, mode);
}

void rdmapmemobj_close(uint16_t node_id, uint64_t pool_id)
{
    client->CloseRemotePool(node_id, pool_id);
}

PMEMoid rdmapmemobj_root(uint64_t pool_id, size_t size)
{
    return client->RemotePoolRoot(pool_id, size);
}

void rdmapmem_direct_read(PMEMoid oid, size_t size, void* result)
{
    void* ret = nullptr;
    if ((ret = pmemobj_direct(oid))) // localdata
    {
        memcpy(result, ret, size);
    }
    else
    {
        client->GetRemotePoolData(oid.pool_uuid_lo, oid.off, size, result);
    }
}

void rdmapmem_direct_write(PMEMoid oid, size_t size, void* source)
{
    void* ret = nullptr;
    if ((ret = pmemobj_direct(oid))) // local data
    {
        PMEMobjpool* pool = pmemobj_pool_by_oid(oid);
        pmemobj_memcpy_persist(pool, ret, source, size);
        //  memcpy(ret, source, size);
    }
    else
    {
        client->WriteRemotePoolData(oid.pool_uuid_lo, oid.off, size, source);
    }
}

void DisConnectServer()
{
    delete client;
}