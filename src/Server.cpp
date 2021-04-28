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
        Debug::notifyInfo("Server Alloc Memory size %d successd at %p",
                          buf_size_, addr_);
    }
    rdmasocket_ = new RdmaSocket(addr_, buf_size_, conf_, true, 0, sock_port,
                                 device_name, rdma_port); // RC
    is_running_ = true;

    Listen();
}

Server::~Server()
{
    Debug::notifyInfo("Stop RPCServer.");
    is_running_ = false;
    if (conf_)
    {
        delete conf_;
        conf_ = NULL;
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
    peers.clear();

    for (auto itr = poll_request.begin(); itr != poll_request.end(); itr++)
    {
        itr->second->join();
    }

    poll_request.clear();

    if (rdmasocket_)
    {
        delete rdmasocket_;
        rdmasocket_ = NULL;
    }

    Debug::notifyInfo("RPCServer is closed successfully.");
}

bool Server::AddPool(uint64_t pool_id, uint16_t node_id, uint64_t va)
{
    std::unique_lock<std::mutex> mlock(m);
    if (pool_info_.find(pool_id) != pool_info_.end())
    {
        Debug::notifyError("The Pool %ld had been created by node %d ,VA :%lld",
                           pool_id, pool_info_.find(pool_id)->second->node_id_,
                           pool_id,
                           pool_info_.find(pool_id)->second->virtual_address_);
        mlock.unlock();
        cond.notify_one();
        return false;
    }

    PoolInfo* new_pool = new PoolInfo(node_id, va);
    pool_info_.insert(PoolPair(pool_id, new_pool));
    mlock.unlock();
    cond.notify_one();
    return true;
}

PoolInfo* Server::GetPool(uint64_t pool_id)
{
    std::unique_lock<std::mutex> mlock(m);
    ID2POOL::iterator itr = pool_info_.find(pool_id);
    if (itr != pool_info_.end())
    {
        Debug::notifyInfo("Find the pool %ld in node %d, va: %p", pool_id,
                          itr->second->node_id_, itr->second->virtual_address_);
        return pool_info_.find(pool_id)->second;
    }
    Debug::notifyError("Don't exist the pool %d", pool_id);
    mlock.unlock();
    cond.notify_one();
    return NULL;
}

bool Server::DeletePool(uint64_t pool_id)
{
    std::unique_lock<std::mutex> mlock(m);
    ID2POOL::iterator itr = pool_info_.find(pool_id);
    if (itr != pool_info_.end())
    {
        Debug::notifyInfo(
            "DeletePool: find the pool %ld, VA: %p, node_id is %d", pool_id,
            itr->second->virtual_address_, itr->second->node_id_);
        pool_info_.erase(pool_id);
        return true;
    }
    Debug::notifyError("Don't exist the pool %d", pool_id);
    mlock.unlock();
    cond.notify_one();
    return false;
}

void Server::Accecpt(int sock)
{
    struct sockaddr_in remote_address;
    int fd;
    // struct timespec start, end;
    socklen_t sin_size = sizeof(struct sockaddr_in);
    while (is_running_
           && (fd = accept(sock, (struct sockaddr*)&remote_address, &sin_size))
                  != -1)
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
                rdmasocket_->RdmaRecv(peer->qp[0], peer->my_buf_addr_ + FOURMB,
                                      FOURMB);
            }
            std::thread* poll_cq_
                = new std::thread(&Server::ProcessRequest, this, peer);
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
        Debug::notifyInfo("IS_RUNNING");
        struct ibv_wc wc[1];
        int ret = 0;
        if ((ret = rdmasocket_->PollCompletion(peer->cq, 1, wc)) <= 0)
        {
            // failed
        }
        else
        {
            Debug::notifyInfo("wc->op_code is %d", wc->opcode);
            switch (wc->opcode) // 对于server 应该只有send recv
            {
            case IBV_WC_RECV:
                // std::cout << (char*)GetClientRecvBaseAddr(peer->node_id)
                //           << std::endl;

                // break;
            case IBV_WC_RECV_RDMA_WITH_IMM:
                //debug
                std::cout << (char*)GetClientRecvBaseAddr(peer->node_id)
                          << std::endl;
                Debug::notifyInfo("Peer->node_id is %d, Recv node id is %d",
                                  peer->node_id, wc->imm_data);
                ProcessRecv(peer->node_id);
                break;
            case IBV_WC_SEND: break;
            case IBV_WC_RDMA_WRITE: break;
            case IBV_WC_RDMA_READ: break;
            default: break;
            }
        }
    }
}

bool Server::ProcessRecv(uint16_t node_id)
{
    PeerConnection* peer = peers[node_id];
    ibv_wc wc[1];
    if (peer)
    {
        Debug::notifyInfo("ProcessRecv: Recv Message From Node %d", node_id);
        printf("my recv's address is %p\n",
               (void*)(peer->my_buf_addr_ + FOURMB));
        Request* recv = (Request*)(peer->my_buf_addr_ + FOURMB);
        if (recv->type_ == CREATEPOOL)
        {
            CreatePool create_pool_req
                = *(CreatePool*)(peer->my_buf_addr_ + FOURMB);
            Debug::notifyInfo(
                "Client %d request add a pool: poolid %d, virtual address: %p",
                node_id, create_pool_req.pool_id_,
                create_pool_req.virtual_addr_);
            if (AddPool(create_pool_req.pool_id_, create_pool_req.node_id,
                        create_pool_req.virtual_addr_))
            {
                Debug::notifyInfo("Create Pool Success");
                Response response;
                response.op_ret_ = SUCCESS;
                uint64_t send_base = peer->my_buf_addr_;
                memcpy((void*)send_base, &response, sizeof(Response));
                rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                                      sizeof(Response));
                if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
                {
                    return true;
                }
            }
            else
            {
                Debug::notifyInfo("Create Pool Failed");
                Response response;
                response.op_ret_ = FAIL;
                uint64_t send_base = peer->my_buf_addr_;
                memcpy((void*)send_base, &response, sizeof(Response));
                rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                                      sizeof(Response));
                if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
                {
                    return true;
                }
            }
        }
        else if (recv->type_ == DELETEPOOL)
        {
            struct DeletePool delete_pool_cq
                = *(struct DeletePool*)(peer->my_buf_addr_ + FOURMB);
            Debug::notifyInfo("Client %d request delete a pool: poolid %d",
                              node_id, delete_pool_cq.pool_id_);
            if (DeletePool(delete_pool_cq.pool_id_))
            {
                Debug::notifyInfo("Delete Pool Success");
                Response response;
                response.op_ret_ = SUCCESS;
                uint64_t send_base = peer->my_buf_addr_;
                memcpy((void*)send_base, &response, sizeof(Response));
                rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                                      sizeof(Response));
                if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
                {
                    Debug::notifyInfo("Reponse Delete Pool Success");
                    return true;
                }
                else
                {
                    Debug::notifyInfo("Reponse Delete Pool Failed");
                    return false;
                }
            }
            else
            {
                Debug::notifyInfo("Delete Pool Failed");
                Response response;
                response.op_ret_ = FAIL;
                uint64_t send_base = peer->my_buf_addr_;
                memcpy((void*)send_base, &response, sizeof(Response));
                rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                                      sizeof(Response));
                if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
                {
                    return true;
                }
            }
        }
        else if (recv->type_ == FINDPOOL)
        {
            FindPool find_pool_cq
                = *(struct FindPool*)(peer->my_buf_addr_ + FOURMB);
            Debug::notifyInfo("Client %d request find a pool: poolid %d",
                              node_id, find_pool_cq.pool_id_);

            if (PoolInfo* ret = GetPool(find_pool_cq.pool_id_))
            {
                Debug::notifyInfo("Find Pool Success");
                FindResponse response;
                response.op_ret_ = INFO;
                response.node_id_ = ret->node_id_;
                response.virtual_addr_ = ret->virtual_address_;
                memcpy(response.ip_, peers[ret->node_id_]->peer_ip,
                       sizeof(peers[0]->peer_ip));
                uint64_t send_base = peer->my_buf_addr_;
                memcpy((void*)send_base, &response, sizeof(FindResponse));
                rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                                      sizeof(FindResponse));
                if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
                {
                    Debug::notifyInfo("Reponse Find Pool Success");
                    return true;
                }
                else
                {
                    Debug::notifyInfo("Reponse Find Pool Failed");
                    return false;
                }
            }
            else
            {
                Debug::notifyInfo("Find Pool Failed");
                Response response;
                response.op_ret_ = FAIL;
                uint64_t send_base = peer->my_buf_addr_;
                memcpy((void*)send_base, &response, sizeof(Response));
                rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                                      sizeof(Response));
                if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
                {
                    return true;
                }
            }
        }
        return false;
    }
    Debug::notifyError("ProcessRecv: Peers[node_id] is NULL");
    return false;
}