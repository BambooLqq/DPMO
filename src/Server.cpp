#include "Server.hpp"

Server::Server(int sock_port, std::string config_file_path,
               std::string device_name, uint32_t rdma_port)
{
    conf_ = new Configuration(config_file_path);
    addr_ = 0;
    buf_size_ = FOURMB * (MAX_CLIENT_NUM + 1) * 2;
    int ret = posix_memalign((void**)&addr_, PAGESIZE, buf_size_);

    if (ret != 0 || (void*)addr_ == NULL)
    {
        Debug::notifyError("Server Alloc Memory size %d failed", buf_size_);
    }
    else
    {
        Debug::notifyInfo("Server Alloc Memory size %d successd at %p", buf_size_, addr_);
    }
    rdmasocket_ = new RdmaSocket(addr_, buf_size_, conf_, true, 0,
                                 sock_port, device_name, rdma_port); // RC
    is_running_ = true;

    Listen();
}

Server::~Server()
{
    Debug::notifyInfo("Stop RPCServer.");
    if (conf_)
    {
        delete conf_;
        conf_ = NULL;
    }
    if (rdmasocket_)
    {
        delete rdmasocket_;
        rdmasocket_ = NULL;
    }
    if ((void*)addr_)
    {
        free((void*)addr_);
    }

    if (pool_info_.size())
    {
        for (auto itr = pool_info_.begin(); itr != pool_info_.end(); itr++)
        {
            delete itr->second;
        }
        pool_info_.clear();
    }
    for (auto itr = peers.begin(); itr != peers.end(); itr++)
    {
        delete itr->second;
        itr->second = NULL;
    }

    for (auto itr = poll_request.begin(); itr != poll_request.end(); itr++)
    {
        itr->second->join();
    }
    poll_request.clear();
    peers.clear();
    Debug::notifyInfo("RPCServer is closed successfully.");
}

bool Server::AddPool(uint32_t pool_id, uint16_t node_id, uint64_t va)
{
    if (pool_info_.find(pool_id) != pool_info_.end())
    {
        Debug::notifyError("The Pool %d had been created by node %d ,VA :%lld",
                           pool_id, pool_info_.find(pool_id)->second->node_id_,
                           pool_id,
                           pool_info_.find(pool_id)->second->virtual_address_);
        return false;
    }

    PoolInfo* new_pool = new PoolInfo(node_id, va);
    pool_info_.insert(PoolPair(pool_id, new_pool));
    return true;
}

PoolInfo* Server::GetPool(uint32_t pool_id)
{
    if (pool_info_.find(pool_id) != pool_info_.end())
    {
        return pool_info_.find(pool_id)->second;
    }
    Debug::notifyError("Don't exist the pool %d", pool_id);
    return NULL;
}

bool Server::DeletePool(uint32_t pool_id)
{
    if (pool_info_.find(pool_id) != pool_info_.end())
    {
        pool_info_.erase(pool_id);
        return true;
    }
    Debug::notifyError("Don't exist the pool %d", pool_id);
    return false;
}

void Server::Accecpt(int sock)
{
    struct sockaddr_in remote_address;
    int fd;
    // struct timespec start, end;
    socklen_t sin_size = sizeof(struct sockaddr_in);
    while (is_running_ && (fd = accept(sock, (struct sockaddr*)&remote_address, &sin_size)) != -1)
    {
        Debug::notifyInfo("Accept a client");
        PeerConnection* peer = new PeerConnection();
        peer->sock = fd;
        if (rdmasocket_->ConnectQueuePair(peer) == false)
        {
            Debug::notifyError("RDMA connect with error");
            delete peer;
        }
        else
        {
            peer->counter = 0;
            peers[peer->node_id] = peer;
            my_node_id_ = rdmasocket_->GetNodeId();
            Debug::notifyInfo("Client %d Connect Server %d", peer->node_id,
                              my_node_id_);
            /* Rdma Receive in Advance. */
            // 接收recv请求
            for (int i = 0; i < QPS_MAX_DEPTH; i++)
            {
                rdmasocket_->RdmaRecv(peer->qp[0], GetClientRecvBaseAddr(peer->node_id), FOURMB);
            }
            std::thread* poll_cq_ = new std::thread(&Server::ProcessRequest, this, peer);
            poll_request[peer->node_id] = poll_cq_;
            Debug::debugItem("Accepted to Node%d", peer->node_id);
        }
    }
}

void Server::Listen()
{
    int listen_sock = rdmasocket_->RdmaListen();
    Accecpt(listen_sock);
}

PeerConnection* Server::GetPeerConnection(uint16_t nodeid)
{
    std::unordered_map<uint16_t, PeerConnection*>::iterator itr;
    if ((itr = peers.find(nodeid)) != peers.end())
    {
        return itr->second;
    }
    else
    {
        Debug::notifyInfo("Not Connected with nodeid %d", nodeid);
        return NULL;
    }
}

void Server::ProcessRequest(PeerConnection* peer) //
{
    if (peer == NULL)
    {
        Debug::notifyError("ProcessRequest: peer is NULL");
        return;
    }
    while (is_running_)
    {
        struct ibv_wc wc[1];
        int ret = 0;
        if ((ret = rdmasocket_->PollCompletion(peer->cq, 1, wc)) <= 0)
        {
            // failed
        }
        else
        {
            switch (wc->opcode) // 对于server 应该只有send recv
            {
            case IBV_WC_RECV:
                std::cout << (char*)GetClientRecvBaseAddr(peer->node_id) << std::endl;
                break;
            case IBV_WC_RECV_RDMA_WITH_IMM:
                std::cout << (char*)GetClientRecvBaseAddr(peer->node_id) << std::endl;
                break;
            case IBV_WC_SEND:
                break;
            case IBV_WC_RDMA_WRITE:
                break;
            case IBV_WC_RDMA_READ:
                break;
            default:
                break;
            }
        }
        return;
    }
}
