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

    std::unordered_map<uint16_t, std::thread*> poll_request_thread_;

    uint64_t addr_;     // mr_addr
    uint64_t buf_size_; // default (node + 1) * 4MB
    uint16_t my_node_id_;

    bool is_running_;

    bool IsConnected(uint16_t node_id)
    {
        if (peers.find(node_id) == peers.end())
        {
            return false;
        }
        return true;
    }

    uint64_t GetServerSendBaseAddr()
    {
        if (IsConnected(0) == false)
        {
            return 0;
        }

        return peers[0]->my_buf_addr_;
    }

    uint64_t GetServerRecvBaseAddr()
    {
        if (IsConnected(0) == false)
        {
            return 0;
        }
        return peers[0]->my_buf_addr_ + FOURMB;
    }

    uint64_t GetClientSendBaseAddr(uint16_t node_id)
    {
        if (IsConnected(node_id) == false)
        {
            return 0;
        }
        return peers[node_id]->my_buf_addr_;
    }

    uint64_t GetClientRecvBaseAddr(uint16_t node_id)
    {
        if (IsConnected(node_id) == false)
        {
            return 0;
        }
        return peers[node_id]->my_buf_addr_ + FOURMB;
    }

    uint64_t GetPeerRecvBaseAddr(uint64_t node_id)
    {
        if (IsConnected(node_id) == false)
        {
            return 0;
        }
        return peers[node_id]->peer_buf_addr_;
    }

    void Listen(); //等待其他client连接

    void Accept(int sock);

    bool ConnectServer();

    PeerConnection* GetPeerConnection(uint16_t nodeid);

public:
    Client(int sock_port = 0, std::string config_file_path = "",
           std::string device_name = "", uint32_t rdma_port = 1);
    ~Client();

    bool ConnectClient(uint16_t node_id); //连接其他Client

    bool SendMessageToServer();
};

#endif // !_CLIENT_H