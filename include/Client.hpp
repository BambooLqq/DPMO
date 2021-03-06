#ifndef _CLIENT_H
#define _CLIENT_H

#include "RdmaSocket.hpp"
#include "stdlib.h"
#include "Message.hpp"
#include <libpmem.h>
#include <libpmemobj.h>
#include <libpmempool.h>
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

    std::thread listener_;

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

    void ProcessRequest(PeerConnection* peer); // 处理与Nodeid连接的请求

    void ProcessRecv(PeerConnection* peer);

    static void SignalTerm(int sig);

    bool SendGetPoolData(uint16_t node_id, uint64_t virtual_address,
                         uint64_t offset, size_t size, void* result);

    bool SendWritePoolData(uint16_t node_id, uint64_t virtual_address,
                           uint64_t offset, size_t size, void* source);

    uint64_t SendCreateRemotePool(uint16_t node_id, const char* path,
                                  const char* layout, size_t poolsize,
                                  mode_t mode);

    uint64_t SendOpenRemotePool(uint16_t node_id, const char* path,
                                const char* layout);

    PMEMoid SendRemotePoolRoot(uint16_t node_id, uint64_t pool_id, uint64_t va,
                               size_t size);

    bool SendCloseRemotePool(uint16_t nodeid, uint64_t pool_id);

public:
    Client(int sock_port = 0, std::string config_file_path = "",
           std::string device_name = "", uint32_t rdma_port = 1);
    ~Client();

    bool ConnectClient(uint16_t node_id); //连接其他Client

    bool SendCreatePool(uint64_t pool_id, uint64_t virtual_address);

    bool SendDeletePool(uint64_t pool_id);

    bool SendFindPool(uint64_t pool_id, GetRemotePool* result);

    bool GetRemotePoolData(uint64_t pool_id, uint64_t offset, size_t size,
                           void* result);

    bool WriteRemotePoolData(uint64_t pool_id, uint64_t offset, size_t size,
                             void* source);

    uint64_t CreateRemotePool(uint16_t node_id, const char* path,
                              const char* layout, size_t poolsize, mode_t mode);

    uint64_t OpenRemotePool(uint16_t node_id, const char* path,
                            const char* layout);

    void CloseRemotePool(uint16_t node_id, uint64_t pool_id);

    PMEMoid RemotePoolRoot(uint64_t pool_id, size_t size);

    bool SendMessageToServer();
};

#endif // !_CLIENT_H