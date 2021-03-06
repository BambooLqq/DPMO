#ifndef _SERVER_H
#define _SERVER_H

#include "RdmaSocket.hpp"
#include "Message.hpp"

/* 
    // ***********************************************************************************************
// 	+-------+-------+-----+-------+-----+-------+-------+-----+-------+-
// 	| Client 1 Send 4MB |  Client 1 Recv 4MB | ... | Cli_N-1 || Client N+1 || Client N+2 || ...
// 	+-------+-------+-----+-------+-----+-------+-------+-----+-------+-
     */

// 多线程 轮询每个连接的CQ 从而进行相应的处理


class Server
{
    Configuration* conf_;
    RdmaSocket* rdmasocket_;
    std::unordered_map<uint16_t, PeerConnection*> peers; // 连接信息
    std::unordered_map<uint16_t, std::thread*>
        poll_request; // 暂时使用多线程 后续考虑线程池

    uint64_t addr_;     // mr_addr
    uint64_t buf_size_; // default (node + 1) * 8MB
    uint16_t my_node_id_;
    ID2POOL pool_info_;
    bool is_running_;

    std::mutex m;                 //保护pool_info
    std::condition_variable cond; //条件

    std::thread listener_;

    bool AddPool(uint64_t pool_id, uint16_t node_id, uint64_t va);

    bool DeletePool(uint64_t pool_id);

    PoolInfo* GetPool(uint64_t pool_id);

    bool IsConnected(uint16_t node_id)
    {
        if (peers.find(node_id) == peers.end())
        {
            return false;
        }
        return true;
    }

    void Listen();
    void Accecpt(int sock);

    void RdmaQueryQueuePair(uint16_t node_id);

    void ProcessRequest(PeerConnection* peer); // 处理与Nodeid连接的请求

    PeerConnection* GetPeerConnection(uint16_t nodeid);

    bool ProcessRecv(uint16_t node_id);
    
    static void SignalTerm(int sig);

public:
    Server(int sock_port = 0, std::string config_file_path = "",
           std::string device_name = "", uint32_t rdma_port = 1);
    ~Server();

    uint64_t GetClientSendBaseAddr(uint16_t node_id)
    {
        if (IsConnected(node_id == false))
        {
            return 0;
        }
        return peers[node_id]->my_buf_addr_;
    }

    uint64_t GetClientRecvBaseAddr(uint16_t node_id)
    {
        if (IsConnected(node_id == false))
        {
            return 0;
        }
        return peers[node_id]->my_buf_addr_ + FOURMB;
    }

    uint64_t GetPeerRecvBaseAddr(uint16_t node_id)
    {
        if (IsConnected(node_id == false))
        {
            return 0;
        }
        return peers[node_id]->peer_buf_addr_;
    }
};

#endif // !_SeERVER_h