#ifndef _CLIENT_H
#define _CLIENT_H

#include "RdmaSocket.hpp"
#include "stdlib.h"

// for client's mem
// 为每个连接分配4KB的缓冲区
// ***********************************************************************************************
// 	+-------+-------+-----+-------+-----+-------+-------+-----+-------+-
// 	| Server | Cli_1 | ... | Cli_N-1 || Client N+1 || Client N+2 || ...
// 	+-------+-------+-----+-------+-----+-------+-------+-----+-------+-

class Client
{
    Configuration* conf_;
    RdmaSocket* rdmasocket_;
    std::unordered_map<uint16_t, PeerConnection*> peers; // 连接信息

    uint32_t addr_;     // mr_addr
    uint32_t buf_size_; // default (node + 1) * 4MB
    uint16_t my_node_id;

    bool IsConnected(uint16_t node_id)
    {
        if (peers.find(node_id) == peers.end())
        {
            return false;
        }
        return true;
    }

public:
    Client(int sock_port = 0, std::string config_file_path = "",
           std::string device_name = "", uint32_t rdma_port = 1);
    ~Client();
    uint64_t GetServerBaseAddr()
    {
        return addr_;
    }
    uint64_t GetClientBaseAddr(uint16_t node_id);

    bool ConnectServer();

    bool ConnectClient(uint16_t node_id);

    PeerConnection* GetPeerConnection(uint16_t nodeid);
};

#endif // !_CLIENT_H