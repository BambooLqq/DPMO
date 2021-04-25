#ifndef _SERVER_H
#define _SERVER_H

#include "RdmaSocket.h"

class PoolInfo
{
public:
    uint32_t node_id_;
    uint64_t virtual_address_;
    PoolInfo(uint32_t node_id, uint64_t virtual_address)
        : node_id_(node_id), virtual_address_(virtual_address)
    {
    }
};

typedef struct NodeInfo
{
    uint16_t node_id_;
    std::string ip_;
} NodeInfo;

typedef std::unordered_map<uint32_t, PoolInfo*> ID2POOL;
typedef std::pair<uint32_t, PoolInfo*> PoolPair;

class Server
{
    Configuration* conf_;
    RdmaSocket* rdmasocket_;
    uint64_t addr_;     // mr_addr
    uint64_t buf_size_; // default (node + 1) * 4MB
    uint16_t my_node_id;
    ID2POOL pool_info_;

    bool AddPool(uint32_t pool_id, uint16_t node_id, uint64_t va);

    bool DeletePool(uint32_t pool_id);

    PoolInfo* GetPool(uint32_t pool_id);

public:
    Server(int sock_port = 0, std::string config_file_path = "");
    ~Server();
};

#endif // !_SeERVER_h