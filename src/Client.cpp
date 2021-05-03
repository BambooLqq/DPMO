#include "Client.hpp"
#include "malloc.h"

Client::Client(int sock_port, std::string config_file_path,
               std::string device_name, uint32_t rdma_port)
{
    conf_ = new Configuration(config_file_path);
    addr_ = 0;
    buf_size_ = (MAX_CLIENT_NUM + 1) * FOURMB * 2;

    addr_ = reinterpret_cast<uint64_t>(memalign(PAGESIZE, buf_size_));

    if (addr_ == 0)
    {
        Debug::notifyError("Client Alloc Memory size %d failed", buf_size_);
    }
    else
    {
        Debug::notifyInfo("Client Alloc Memory size %d at addr: %p successd",
                          buf_size_, addr_);
        memset((char*)addr_, 0, buf_size_);
    }
    rdmasocket_ = new RdmaSocket(addr_, buf_size_, conf_, false, 0, sock_port,
                                 device_name, rdma_port); // RC
    is_running_ = true;
    if (ConnectServer())
    {
        my_node_id_ = rdmasocket_->GetNodeId();
        Debug::notifyInfo("Client Connects server successfully, node id = %d",
                          my_node_id_);
    }
    else
    {
        Debug::notifyError("Client connects server failed");
    }

    listener_ = std::thread(&Client::Listen, this);
}

Client::~Client()
{
    Debug::notifyInfo("Stop RPCClient.");
    is_running_ = false;
    listener_.join();
    if (conf_)
    {
        delete conf_;
        conf_ = NULL;
    }

    if ((void*)addr_)
    {
        free((void*)addr_);
    }

    for (auto itr = peers.begin(); itr != peers.end(); itr++)
    {
        delete itr->second;
        itr->second = NULL;
    }
    peers.clear();
    Debug::notifyInfo("Peers Clear Successfully");

    for (auto itr = poll_request_thread_.begin();
         itr != poll_request_thread_.end(); itr++)
    {
        // itr->second->detach();
        pthread_t tid = itr->second->native_handle();
        std::cout << "poll_thread_id is: " << tid << std::endl;
        pthread_kill(tid, SIGTERM);
        itr->second->join();
        delete itr->second;
    }
    poll_request_thread_.clear();
    Debug::notifyInfo("poll_request_thread Clear Successfully");

    if (rdmasocket_)
    {
        delete rdmasocket_;
        rdmasocket_ = NULL;
    }

    Debug::notifyInfo("Client is closed successfully.");
}

bool Client::ConnectServer()
{
    if (IsConnected(0))
    {
        Debug::notifyInfo("RdmaConnectServer: Have Connected With Server");
        return true;
    }
    int sock = rdmasocket_->SocketConnect(0);
    if (sock < 0)
    {
        Debug::notifyError("Socket connection failed to server 0");
        return false;
    }
    PeerConnection* peer = new PeerConnection();
    peer->sock = sock;
    /* Add server's NodeID to the structure */
    peer->node_id = 0;
    if (rdmasocket_->ConnectQueuePair(peer) == false)
    {
        Debug::notifyError("RDMA connect with error");
        delete peer;
        return false;
    }
    else
    {
        if (peers[peer->node_id])
        {
            delete peers[peer->node_id];
        }
        peers[peer->node_id] = peer;
        peer->counter = 0;
        Debug::debugItem("Finished Connecting to Node%d", peer->node_id);
        return true;
    }
}

bool Client::ConnectClient(uint16_t node_id)
{
    int sock;
    /* Connect to Node 0 firstly to get clientID. */

    if (peers.find(node_id) != peers.end())
    {
        Debug::notifyInfo(
            "RdmaConnectClient: Have Connected With Client node id %d",
            node_id);
        return true;
    }
    sock = rdmasocket_->SocketConnect(node_id);
    if (sock < 0)
    {
        Debug::notifyError("Socket connection failed to client nodeid %d",
                           node_id);
        return false;
    }
    PeerConnection* peer = new PeerConnection();
    peer->sock = sock;
    /* Add server's NodeID to the structure */
    peer->node_id = node_id;
    if (rdmasocket_->ConnectQueuePair(peer) == false)
    {
        Debug::notifyError("RDMA connect with error");
        delete peer;
        return false;
    }
    else
    {
        if (peers[peer->node_id])
        {
            delete peers[peer->node_id];
        }
        peers[peer->node_id] = peer;
        peer->counter = 0;
        Debug::debugItem("Finished Connecting to Node%d", peer->node_id);
        return true;
    }
}

PeerConnection* Client::GetPeerConnection(uint16_t nodeid)
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

void Client::Accept(int sock)
{
    struct sockaddr_in remote_address;
    int fd;
    // struct timespec start, end;
    socklen_t sin_size = sizeof(struct sockaddr_in);
    struct timeval timeout = {3, 0};
    int ret = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout,
                         sizeof(timeout));
    if (ret < 0) Debug::notifyError("Set timeout failed!");
    while (is_running_
           && (fd = accept(sock, (struct sockaddr*)&remote_address, &sin_size)))
    {
        if (fd == -1)
        {
            if (errno == EWOULDBLOCK)
                continue;
            else
                break;
        }
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
            if (peers[peer->node_id])
            {
                delete peers[peer->node_id];
            }
            peers[peer->node_id] = peer;
            Debug::notifyInfo("Client %d Connect Client %d", peer->node_id,
                              my_node_id_);
            /* Rdma Receive in Advance. */
            // 接收recv请求
            for (int i = 0; i < QPS_MAX_DEPTH; i++)
            {
                rdmasocket_->RdmaRecv(peer->qp[0], peer->my_buf_addr_ + FOURMB,
                                      FOURMB);
            }
            std::thread* poll_cq_
                = new std::thread(&Client::ProcessRequest, this, peer);
            if (poll_request_thread_[peer->node_id])
            {
                pthread_t tid
                    = poll_request_thread_[peer->node_id]->native_handle();
                std::cout << "poll_thread_id is: " << tid << std::endl;
                pthread_kill(tid, SIGTERM);
                poll_request_thread_[peer->node_id]->join();
                delete poll_request_thread_[peer->node_id];
            }
            poll_request_thread_[peer->node_id] = poll_cq_;
            Debug::debugItem("Accepted to Node%d", peer->node_id);
        }
    }
}

void Client::Listen()
{
    int sock_ = rdmasocket_->RdmaListen();
    Accept(sock_);
}

bool Client::SendCreatePool(uint64_t pool_id, uint64_t virtual_address)
{
    CreatePool send;
    ibv_wc wc[1];
    send.node_id = my_node_id_;
    send.pool_id_ = pool_id;
    send.type_ = CREATEPOOL;
    send.virtual_addr_ = virtual_address;
    void* send_base = (void*)(peers[0]->my_buf_addr_);
    memcpy(send_base, &send, sizeof(CreatePool));
    rdmasocket_->RdmaSend(peers[0]->qp[0], (uint64_t)send_base,
                          sizeof(CreatePool));
    if (rdmasocket_->PollCompletion(peers[0]->cq, 1, wc))
    {
        void* recv_base = (void*)((uint64_t)send_base + FOURMB);
        rdmasocket_->RdmaRecv(peers[0]->qp[0], (uint64_t)recv_base, FOURMB);
        if (rdmasocket_->PollCompletion(peers[0]->cq, 1, wc))
        {
            Response* response = (Response*)recv_base;
            if (response->op_ret_ == SUCCESS)
            {
                Debug::notifyInfo("Server response: CreatePool Success!");
                return true;
            }
            else
            {
                Debug::notifyError("Server response: CreatePool Failed!");
                return false;
            }
        }
        else
        {
            Debug::notifyError("Not recv the server's response");
            return false;
        }
    }
    else
    {
        Debug::notifyError("Client send the request failed");
        return false;
    }
}

bool Client::SendDeletePool(uint64_t pool_id)
{
    DeletePool send;
    ibv_wc wc[1];
    send.pool_id_ = pool_id;
    send.type_ = DELETEPOOL;
    void* send_base = (void*)(peers[0]->my_buf_addr_);
    memcpy(send_base, &send, sizeof(DeletePool));
    rdmasocket_->RdmaSend(peers[0]->qp[0], (uint64_t)send_base,
                          sizeof(DeletePool));
    if (rdmasocket_->PollCompletion(peers[0]->cq, 1, wc))
    {
        void* recv_base = (void*)((uint64_t)send_base + FOURMB);
        rdmasocket_->RdmaRecv(peers[0]->qp[0], (uint64_t)recv_base, FOURMB);
        if (rdmasocket_->PollCompletion(peers[0]->cq, 1, wc))
        {
            Response* response = (Response*)recv_base;
            if (response->op_ret_ == SUCCESS)
            {
                Debug::notifyInfo("Server response: DeletePool Success!");
                return true;
            }
            else
            {
                Debug::notifyError("Server response: DeletePool Failed!");
                return false;
            }
        }
        else
        {
            Debug::notifyError("Not recv the server's response");
            return false;
        }
    }
    else
    {
        Debug::notifyError("Client send the request failed");
        return false;
    }
}

bool Client::SendFindPool(uint64_t pool_id, GetRemotePool* result)
{
    FindPool send;
    ibv_wc wc[1];
    send.pool_id_ = pool_id;
    send.type_ = FINDPOOL;
    void* send_base = (void*)(peers[0]->my_buf_addr_);
    memcpy(send_base, &send, sizeof(FindPool));
    rdmasocket_->RdmaSend(peers[0]->qp[0], (uint64_t)send_base,
                          sizeof(FindPool));
    if (rdmasocket_->PollCompletion(peers[0]->cq, 1, wc))
    {
        void* recv_base = (void*)((uint64_t)send_base + FOURMB);
        rdmasocket_->RdmaRecv(peers[0]->qp[0], (uint64_t)recv_base, FOURMB);
        if (rdmasocket_->PollCompletion(peers[0]->cq, 1, wc))
        {
            Response* response = (Response*)recv_base;
            if (response->op_ret_ == INFO)
            {
                Debug::notifyInfo("Server response: FindPool Success!");
                response = (FindResponse*)recv_base;
                result->node_id_ = ((FindResponse*)response)->node_id_;
                result->virtual_address_
                    = ((FindResponse*)response)->virtual_addr_;
                memcpy(result->ip_, ((FindResponse*)response)->ip_,
                       sizeof(FindResponse::ip_));
                Debug::notifyInfo("Pool %ld is in node %d, VA: %p, ip :%s",
                                  pool_id, result->node_id_,
                                  result->virtual_address_, result->ip_);
                return true;
            }
            else
            {
                Debug::notifyError("Server response: FindPool Failed!");
                return false;
            }
        }
        else
        {
            Debug::notifyError("Not recv the server's response");
            return false;
        }
    }
    else
    {
        Debug::notifyError("Client send the request failed");
        return false;
    }
}

void Client::SignalTerm(int sig)
{
    std::cout << "Client::ProcessRequest thread id is "
              << std::this_thread::get_id() << std::endl;
    pthread_exit(NULL);
}

void Client::ProcessRequest(PeerConnection* peer)
{
    if (peer == NULL)
    {
        Debug::notifyError("ProcessRequest: peer is NULL");
        return;
    }
    signal(SIGTERM, Client::SignalTerm);
    while (is_running_)
    {
        Debug::notifyInfo("Client Is Processing Request");
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
                Debug::notifyInfo("Peer->node_id is %d, Recv node id is %d",
                                  peer->node_id, wc->imm_data);
                ProcessRecv(peer);
                break;
            case IBV_WC_SEND: break;
            case IBV_WC_RDMA_WRITE: break;
            case IBV_WC_RDMA_READ: break;
            default: break;
            }
        }
    }
    Debug::notifyInfo("Client: ProcessRequest node %d exit!", peer->node_id);
}

void Client::ProcessRecv(PeerConnection* peer)
{
    void* recv_base = (void*)(peer->my_buf_addr_ + FOURMB);
    Request* recv = (Request*)recv_base;
    if (recv->type_ == GETPOOLDATA)
    {
        GetPoolData get_pool_data = *(GetPoolData*)recv_base;
        Debug::notifyInfo(
            "Client %d request a pool data, va: %p, offset: %llu,size: %llu",
            peer->node_id, get_pool_data.virtual_address_,
            get_pool_data.offset_, get_pool_data.size_);
        void* send_base = (void*)peer->my_buf_addr_;
        memset(send_base, 0, FOURMB);
        char temp[64];
        memset(temp, 0, sizeof(temp));
        sprintf(
            temp, "%p",
            (void*)(get_pool_data.virtual_address_ + get_pool_data.offset_));
        memcpy(send_base, temp, sizeof(temp));
        void* send = (void*)((uint64_t)send_base + sizeof(temp));
        memcpy(send,
               (void*)(get_pool_data.virtual_address_ + get_pool_data.offset_),
               get_pool_data.size_);
        void* send_data_end = (void*)((uint64_t)send + get_pool_data.size_);
        memcpy(send_data_end, temp, sizeof(temp));
        std::cout << "RemoteWrite: " << (char*)send_base << " " << (char*)send
                  << " " << (char*)send_data_end << std::endl;
        rdmasocket_->RemoteWrite(peer, (uint64_t)send_base, 0,
                                 2 * sizeof(temp) + get_pool_data.size_);
    }
    else if (recv->type_ == WRITEPOOLDATA)
    {
        WritePoolData write_pool_data = *(WritePoolData*)recv_base;
        uint64_t va = write_pool_data.virtual_address_;
        uint64_t offset = write_pool_data.offset_;
        uint64_t size = write_pool_data.size_;

        Debug::notifyInfo(
            "Client %d request Write a pool data, va: %p, offset: %llu,size: %llu",
            peer->node_id, va, offset, size);

        char temp[64];
        memset(temp, 0, sizeof(temp));
        sprintf(temp, "%p", (void*)(va + offset));
        void* recv_data = (void*)((uint64_t)recv_base + sizeof(WritePoolData));
        void* recv_data_begin = (void*)((uint64_t)recv_data + sizeof(temp));
        void* recv_data_end
            = (void*)((uint64_t)recv_data + size + sizeof(temp));
        while (memcmp(recv_data, temp, sizeof(temp)))
            ;
        while (memcmp(recv_data_end, temp, sizeof(temp)))
            ;
        memcpy((void*)(va + offset), recv_data_begin, size); // need to persisit
    }
    else if (recv->type_ == CREATEREMOTEPOOL)
    {
        struct CreateRemotePool create_pool
            = *(struct CreateRemotePool*)recv_base;
        const char* path = create_pool.path;
        const char* layout = create_pool.layout;
        size_t poolsize = create_pool.poolsize;
        Debug::notifyInfo(
            "Client %d request create a pool, path: %s, layout: %s, size: %lu",
            peer->node_id, path, layout, poolsize);

        PMEMobjpool* pool = NULL;
        CreateRemotePoolResponse response;
        if (pool = pmemobj_create(path, layout, poolsize, create_pool.mode))
        {
            struct Root
            {
                int size;
            };
            PMEMoid root = pmemobj_root(pool, sizeof(Root));
            uint64_t pool_id = root.pool_uuid_lo;
            pmemobj_free(&root);
            Debug::notifyInfo("Create a pool Successfully, poolid is %lu",
                              pool_id);
            SendCreatePool(pool_id, (uint64_t)pool);
            response.op_ret_ = SUCCESS;
            response.pool_id_ = pool_id;
        }
        else
        {
            Debug::notifyInfo("Create a pool Failed");
            response.op_ret_ = FAIL;
        }
        uint64_t send_base = peer->my_buf_addr_;
        memcpy((void*)send_base, &response, sizeof(CreateRemotePoolResponse));
        rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                              sizeof(CreateRemotePoolResponse));
        ibv_wc wc[1];
        if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
        {
            Debug::notifyInfo("Response Client %d Createpool: success",
                              peer->node_id);
        }
        else
        {
            Debug::notifyInfo("Response Client %d Createpool: failed",
                              peer->node_id);
        }
    }
    else if (recv->type_ == OPENREMOTEPOOL)
    {
        struct OpenRemotePool open_pool = *(struct OpenRemotePool*)recv_base;
        const char* path = open_pool.path;
        const char* layout = open_pool.layout;

        Debug::notifyInfo("Client %d request open a pool, path: %s, layout: %s",
                          peer->node_id, path, layout);

        PMEMobjpool* pop = NULL;
        OpenRemotePoolResponse response;

        if (pop = pmemobj_open(path, layout))
        {
            size_t size = pmemobj_root_size(pop);
            uint64_t pool_id;
            if (size)
            {
                PMEMoid root = pmemobj_root(pop, size);
                pool_id = root.pool_uuid_lo;
            }
            else
            {
                struct Root
                {
                    int size;
                };
                PMEMoid root = pmemobj_root(pop, sizeof(Root));
                pool_id = root.pool_uuid_lo;
                pmemobj_free(&root);
            }
            Debug::notifyInfo("Open a pool Successfully, poolid is %lu",
                              pool_id);
            SendCreatePool(pool_id, (uint64_t)pop);
            response.op_ret_ = SUCCESS;
            response.pool_id_ = pool_id;
        }
        else
        {
            Debug::notifyInfo("Open a pool Failed");
            response.op_ret_ = FAIL;
        }
        uint64_t send_base = peer->my_buf_addr_;
        memcpy((void*)send_base, &response, sizeof(OpenRemotePoolResponse));
        rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                              sizeof(OpenRemotePoolResponse));
        ibv_wc wc[1];
        if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
        {
            Debug::notifyInfo("Response Client %d OpenPool: success",
                              peer->node_id);
        }
        else
        {
            Debug::notifyInfo("Response Client %d OpenPool: failed",
                              peer->node_id);
        }
    }
    else if (recv->type_ == CLOSEREMOTEPOOL)
    {
        struct CloseRemotePool open_pool = *(struct CloseRemotePool*)recv_base;
        uint64_t pool_id = open_pool.pool_id_;
        Debug::notifyInfo("Client %d request close a pool,pool_id: %llu",
                          peer->node_id, pool_id);
        GetRemotePool result;
        PMEMobjpool* pop = NULL;
        Response response;
        if (SendFindPool(pool_id, &result))
        {
            Debug::notifyInfo("Find the pool: poolid: %llu, va: %p", pool_id,
                              result.virtual_address_);
            pop = (PMEMobjpool*)result.virtual_address_;
            SendDeletePool(pool_id);
            pmemobj_close(pop);
            Debug::notifyInfo("Close a pool Successfully, poolid is %lu",
                              pool_id);
            response.op_ret_ = SUCCESS;
        }
        else
        {
            Debug::notifyInfo("Find the pool: poolid: %llu, failed", pool_id);
            response.op_ret_ = FAIL;
        }
        uint64_t send_base = peer->my_buf_addr_;
        memcpy((void*)send_base, &response, sizeof(Response));
        rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                              sizeof(Response));
        ibv_wc wc[1];
        if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
        {
            Debug::notifyInfo("Response Client %d Close result: success",
                              peer->node_id);
        }
        else
        {
            Debug::notifyInfo("Response Client %d ClosePool: failed",
                              peer->node_id);
        }
    }
    else if (recv->type_ == REMOTEPOOLROOT) //创建root
    {
        struct RemotePoolRoot pool_root = *(struct RemotePoolRoot*)recv_base;
        uint64_t pool_id = pool_root.pool_id_;
        size_t size = pool_root.size_;
        uint64_t va = pool_root.va_;
        Debug::notifyInfo(
            "Client %d request a pool root, pool_id: %llu, size: %lu",
            peer->node_id, pool_id, size);

        GetRemotePool result;
        PMEMobjpool* pop = (PMEMobjpool*)va;
        RemotePoolRootReponse response;
        response.op_ret_ = SUCCESS;
        response.oid_ = pmemobj_root(pop, size);
        uint64_t send_base = peer->my_buf_addr_;
        memcpy((void*)send_base, &response, sizeof(Response));
        rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                              sizeof(Response));
        ibv_wc wc[1];
        if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
        {
            Debug::notifyInfo("Response Client %d Get Rootobj result: success",
                              peer->node_id);
        }
        else
        {
            Debug::notifyInfo("Response Client %d Get Rootobj result: failed",
                              peer->node_id);
        }
    }
}

bool Client::SendGetPoolData(uint16_t node_id, uint64_t virtual_address,
                             uint64_t offset, size_t size, void* result)
{
    GetPoolData send;
    ibv_wc wc[1];
    send.virtual_address_ = virtual_address;
    send.offset_ = offset;
    send.size_ = size;
    send.type_ = GETPOOLDATA;
    PeerConnection* peer = peers[node_id];

    void* send_base = (void*)(peer->my_buf_addr_);
    void* recv_base = (void*)((uint64_t)send_base + FOURMB);
    memset(recv_base, 0, FOURMB);
    memcpy(send_base, &send, sizeof(GetPoolData));
    rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                          sizeof(GetPoolData));
    if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
    {
        char temp[64];
        memset(temp, 0, sizeof(temp));
        sprintf(temp, "%p", (void*)(virtual_address + offset));
        while (memcmp(recv_base, temp, sizeof(temp)))
            ; //收到数据头部
        while (memcmp((void*)((uint64_t)recv_base + size + sizeof(temp)), temp,
                      sizeof(temp)))
            ; //直到收到数据尾部
        memcpy(result, (void*)((uint64_t)recv_base + sizeof(temp)),
               size); //将数据复制到结果
        return true;
    }
    else
    {
        Debug::notifyError("Client send the request failed");
        return false;
    }
}

bool Client::GetRemotePoolData(uint64_t pool_id, uint64_t offset, size_t size,
                               void* result)
{
    GetRemotePool remote_pool_info;
    if (SendFindPool(pool_id, &remote_pool_info))
    {
        if (ConnectClient(remote_pool_info.node_id_))
        {
            if (SendGetPoolData(remote_pool_info.node_id_,
                                remote_pool_info.virtual_address_, offset, size,
                                result))
            {
                return true;
            }
            else
            {
                Debug::notifyError("GetRemotePoolData: SendGetPoolData \
                    Failed: node_id:%d, va: %p, offset: %llu, size: %llu",
                                   remote_pool_info.node_id_,
                                   remote_pool_info.virtual_address_, offset,
                                   size);
                return false;
            }
        }
        else
        {
            Debug::notifyError("GetRemotePoolData: Connect Client %d: Failed",
                               remote_pool_info.node_id_);
            return false;
        }
    }
    else
    {
        Debug::notifyError(
            "GetRemotePoolData: poolid: %llu,SendFindPool Failed", pool_id);
        return false;
    }
}

bool Client::SendWritePoolData(uint16_t node_id, uint64_t virtual_address,
                               uint64_t offset, size_t size, void* source)
{
    WritePoolData send;
    ibv_wc wc[1];
    send.type_ = WRITEPOOLDATA;
    send.virtual_address_ = virtual_address;
    send.offset_ = offset;
    send.size_ = size;

    PeerConnection* peer = peers[node_id];
    void* send_base = (void*)(peer->my_buf_addr_);
    memcpy(send_base, &send, sizeof(WritePoolData));
    rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                          sizeof(WritePoolData));
    if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
    {
        char temp[64];
        void* send_data = (void*)((uint64_t)send_base + sizeof(temp));
        void* send_data_end = (void*)((uint64_t)send_data + size);
        memset(send_base, 0, FOURMB);
        memset(temp, 0, sizeof(temp));
        sprintf(temp, "%p", (void*)(virtual_address + offset));
        memcpy(send_base, temp, sizeof(temp));
        memcpy(send_data, source, size);
        memcpy(send_data_end, temp, sizeof(temp));
        std::cout << "Remote Write to node : " << node_id << ": "
                  << (char*)send_base << " " << send_data << ": "
                  << (char*)send_data_end << std::endl;
        rdmasocket_->RemoteWrite(peer, (uint64_t)send_base,
                                 sizeof(WritePoolData),
                                 sizeof(temp) * 2 + size);
        return true;
    }
    else
    {
        Debug::notifyError("Client send the Write Pool Data request Failed");
        return false;
    }
}

bool Client::WriteRemotePoolData(uint64_t pool_id, uint64_t offset, size_t size,
                                 void* source)
{
    GetRemotePool remote_pool_info;
    if (SendFindPool(pool_id, &remote_pool_info))
    {
        if (ConnectClient(remote_pool_info.node_id_))
        {
            if (SendWritePoolData(remote_pool_info.node_id_,
                                  remote_pool_info.virtual_address_, offset,
                                  size, source))
            {
                return true;
            }
            else
            {
                Debug::notifyError("WriteRemotePoolData: SendWritePoolData \
                    Failed: node_id:%d, va: %p, offset: %llu, size: %llu",
                                   remote_pool_info.node_id_,
                                   remote_pool_info.virtual_address_, offset,
                                   size);
                return false;
            }
        }
        else
        {
            Debug::notifyError("WriteRemotePoolData: Connect Client %d: Failed",
                               remote_pool_info.node_id_);
            return false;
        }
    }
    else
    {
        Debug::notifyError(
            "WriteRemotePoolData: poolid: %llu,SendFindPool Failed", pool_id);
        return false;
    }
}

bool Client::SendMessageToServer()
{
    char buf[128];
    struct ibv_wc wc[1];
    const char hello[20] = "Hello Server";
    memcpy(buf, hello, sizeof(hello));
    memcpy((void*)GetServerSendBaseAddr(), buf, sizeof(buf));

    rdmasocket_->RdmaSend(peers[0]->qp[0], GetServerSendBaseAddr(),
                          sizeof(buf));
    if (rdmasocket_->PollCompletion(peers[0]->cq, 1, wc))
    {
        std::cout << "Send Success" << std::endl;
    }
    return true;
}

uint64_t Client::SendCreateRemotePool(uint16_t node_id, const char* path,
                                      const char* layout, size_t poolsize,
                                      mode_t mode)
{
    struct CreateRemotePool send;
    ibv_wc wc[1];
    send.type_ = CREATEREMOTEPOOL;
    memcpy(send.path, path, strlen(path) + 1);
    memcpy(send.layout, layout, strlen(layout) + 1);
    send.poolsize = poolsize;
    send.mode = mode;

    PeerConnection* peer = peers[node_id];

    void* send_base = (void*)(peer->my_buf_addr_);
    memcpy(send_base, &send, sizeof(struct CreateRemotePool));
    rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                          sizeof(struct CreateRemotePool));
    if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
    {
        void* recv_base = (void*)((uint64_t)send_base + FOURMB);
        rdmasocket_->RdmaRecv(peers[0]->qp[0], (uint64_t)recv_base, FOURMB);
        if (rdmasocket_->PollCompletion(peers[0]->cq, 1, wc))
        {
            Response* response = (Response*)recv_base;
            if (response->op_ret_ == SUCCESS)
            {
                uint64_t ret_pool_id
                    = ((CreateRemotePoolResponse*)recv_base)->pool_id_;
                Debug::notifyInfo(
                    "client %d response: CreatePool Success, poolid is :%llu",
                    node_id, ret_pool_id);
                return ret_pool_id;
            }
            else
            {
                Debug::notifyError("client %d response: CreatePool Failed!",
                                   node_id);
                return 0;
            }
        }
        else
        {
            Debug::notifyError("Not recv the client's:%d response", node_id);
            return 0;
        }
    }
    else
    {
        Debug::notifyError("Client send the request: CreateRemotePool failed");
        return 0;
    }
}

uint64_t Client::SendOpenRemotePool(uint16_t node_id, const char* path,
                                    const char* layout)
{
    struct OpenRemotePool send;
    ibv_wc wc[1];
    send.type_ = OPENREMOTEPOOL;
    memcpy(send.path, path, strlen(path) + 1);
    memcpy(send.layout, layout, strlen(layout) + 1);

    PeerConnection* peer = peers[node_id];

    void* send_base = (void*)(peer->my_buf_addr_);
    memcpy(send_base, &send, sizeof(struct OpenRemotePool));
    rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                          sizeof(struct OpenRemotePool));
    if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
    {
        void* recv_base = (void*)((uint64_t)send_base + FOURMB);
        rdmasocket_->RdmaRecv(peers[0]->qp[0], (uint64_t)recv_base, FOURMB);
        if (rdmasocket_->PollCompletion(peers[0]->cq, 1, wc))
        {
            Response* response = (Response*)recv_base;
            if (response->op_ret_ == SUCCESS)
            {
                uint64_t ret_pool_id
                    = ((OpenRemotePoolResponse*)recv_base)->pool_id_;
                Debug::notifyInfo(
                    "client %d response: OpenPool Success, poolid is :%llu",
                    node_id, ret_pool_id);
                return ret_pool_id;
            }
            else
            {
                Debug::notifyError("client %d response: OpenPool Failed!",
                                   node_id);
                return 0;
            }
        }
        else
        {
            Debug::notifyError("Not recv the client's:%d response", node_id);
            return 0;
        }
    }
    else
    {
        Debug::notifyError("Client send the request: OpenPool failed");
        return 0;
    }
}

bool Client::SendCloseRemotePool(uint16_t node_id, uint64_t pool_id)
{
    struct CloseRemotePool send;
    ibv_wc wc[1];
    send.type_ = CLOSEREMOTEPOOL;
    send.pool_id_ = pool_id;

    PeerConnection* peer = peers[node_id];

    void* send_base = (void*)(peer->my_buf_addr_);
    memcpy(send_base, &send, sizeof(struct CloseRemotePool));
    rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                          sizeof(struct CloseRemotePool));
    if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
    {
        void* recv_base = (void*)((uint64_t)send_base + FOURMB);
        rdmasocket_->RdmaRecv(peers[0]->qp[0], (uint64_t)recv_base, FOURMB);
        if (rdmasocket_->PollCompletion(peers[0]->cq, 1, wc))
        {
            Response* response = (Response*)recv_base;
            if (response->op_ret_ == SUCCESS)
            {
                Debug::notifyInfo(
                    "client %d response: ClosePool Success, poolid is :%llu",
                    pool_id);
                return true;
            }
            else
            {
                Debug::notifyError("client %d response: ClosePool Failed!",
                                   node_id);
                return false;
            }
        }
        else
        {
            Debug::notifyError("Not recv the client's:%d response", node_id);
            return false;
        }
    }
    else
    {
        Debug::notifyError("Client send the request: ClosePool failed");
        return false;
    }
}

uint64_t Client::CreateRemotePool(uint16_t node_id, const char* path,
                                  const char* layout, size_t poolsize,
                                  mode_t mode)
{
    if (node_id == 0)
    {
        PMEMobjpool* pool = NULL;
        if ((pool = pmemobj_create(path, layout, poolsize, mode)))
        {
            struct Root
            {
                int size;
            };
            PMEMoid root = pmemobj_root(pool, sizeof(Root));
            uint64_t pool_id = root.pool_uuid_lo;
            pmemobj_free(&root);
            SendCreatePool(pool_id, (uint64_t)pool);
            return pool_id;
        }
        return 0;
    }
    else if (!IsConnected(node_id))
    {
        if (ConnectClient(node_id) == false)
        {
            Debug::notifyError("CreateRemotePool: Connect Client %d: Failed",
                               node_id);
            return 0;
        }
    }
    return SendCreateRemotePool(node_id, path, layout, poolsize, mode);
}

uint64_t Client::OpenRemotePool(uint16_t node_id, const char* path,
                                const char* layout)
{
    if (node_id == 0)
    {
        PMEMobjpool* pool = NULL;
        if ((pool = pmemobj_open(path, layout)))
        {
            size_t size = pmemobj_root_size(pool);
            uint64_t pool_id;
            if (size)
            {
                PMEMoid root = pmemobj_root(pool, size);
                pool_id = root.pool_uuid_lo;
            }
            else
            {
                struct Root
                {
                    int size;
                };
                PMEMoid root = pmemobj_root(pool, sizeof(Root));
                pool_id = root.pool_uuid_lo;
                pmemobj_free(&root);
            }
            SendCreatePool(pool_id, (uint64_t)pool);
            return pool_id;
        }
        return 0;
    }
    else if (!IsConnected(node_id))
    {
        if (ConnectClient(node_id) == false)
        {
            Debug::notifyError("OpenRemotePool: Connect Client %d: Failed",
                               node_id);
            return 0;
        }
    }
    return SendOpenRemotePool(node_id, path, layout);
}

void Client::CloseRemotePool(uint16_t node_id, uint64_t pool_id)
{
    if (node_id == 0) //local pool
    {
        GetRemotePool result;
        if (SendFindPool(pool_id, &result))
        {
            PMEMobjpool* pop = (PMEMobjpool*)(result.virtual_address_);
            SendDeletePool(pool_id);
            pmemobj_close(pop);
            Debug::notifyInfo("Close pool %llu successfullt", pool_id);
        }
        Debug::notifyError("Not Find pool %llu", pool_id);
        return;
    }
    else if (!IsConnected(node_id))
    {
        if (ConnectClient(node_id) == false)
        {
            Debug::notifyError("CloseRemotePool: Connect Client %d: Failed",
                               node_id);
        }
    }
    SendCloseRemotePool(node_id, pool_id);
}

PMEMoid Client::SendRemotePoolRoot(uint16_t node_id, uint64_t pool_id,
                                   uint64_t va, size_t size)
{
    ibv_wc wc[1];
    struct RemotePoolRoot send;
    send.type_ = REMOTEPOOLROOT;
    send.pool_id_ = pool_id;
    send.size_ = size;
    send.va_ = va;
    PMEMoid ret;
    memset(&ret, 0, sizeof(PMEMoid));
    PeerConnection* peer = peers[node_id];

    void* send_base = (void*)(peer->my_buf_addr_);
    void* recv_base = (void*)((uint64_t)send_base + FOURMB);
    memset(recv_base, 0, FOURMB);
    memcpy(send_base, &send, sizeof(struct RemotePoolRoot));
    rdmasocket_->RdmaSend(peer->qp[0], (uint64_t)send_base,
                          sizeof(struct RemotePoolRoot));
    if (rdmasocket_->PollCompletion(peer->cq, 1, wc))
    {
        void* recv_base = (void*)((uint64_t)send_base + FOURMB);
        rdmasocket_->RdmaRecv(peers[0]->qp[0], (uint64_t)recv_base, FOURMB);
        if (rdmasocket_->PollCompletion(peers[0]->cq, 1, wc))
        {
            Response* response = (Response*)recv_base;
            if (response->op_ret_ == SUCCESS)
            {
                ret = ((RemotePoolRootReponse*)recv_base)->oid_;
                Debug::notifyInfo(
                    "client %d response: PoolRoot Success, poolid is :%llu, offset: %llu",
                    node_id, ret.pool_uuid_lo, ret.off);
                return ret;
            }
            else
            {
                Debug::notifyError("client %d response: OpenPoolRoot Failed!",
                                   node_id);
                return ret;
            }
        }
        else
        {
            Debug::notifyError("Not recv the client's:%d response", node_id);
            return ret;
        }
    }
    else
    {
        Debug::notifyError(
            "Client send the request: SendRemotePoolRoot failed");
        return ret;
    }
}

PMEMoid Client::RemotePoolRoot(uint64_t pool_id, size_t size)
{
    GetRemotePool result;
    if (SendFindPool(pool_id, &result))
    {
        PMEMobjpool* pop = (PMEMobjpool*)(result.virtual_address_);
        Debug::notifyInfo("Close pool %llu successfullt", pool_id);
        if (result.node_id_ == my_node_id_) // local pool
        {
            return pmemobj_root(pop, size);
        }
        else
        {
            return SendRemotePoolRoot(result.node_id_, pool_id,
                                      result.virtual_address_, size);
        }
    }
    else
    {
        Debug::notifyError("Not Find pool %llu", pool_id);
        return;
    }
}