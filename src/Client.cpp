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
    listener_.detach();
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

    if (rdmasocket_)
    {
        delete rdmasocket_;
        rdmasocket_ = NULL;
    }
    for (auto itr = poll_request_thread_.begin();
         itr != poll_request_thread_.end(); itr++)
    {
        itr->second->detach();
    }

    poll_request_thread_.clear();
    Debug::notifyInfo("poll_request_thread Clear Successfully");

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
        std::cout << "bengin2" << std::endl;
        peers[peer->node_id] = peer;
        peer->counter = 0;
        std::cout << "bengin2" << std::endl;
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

void Client::ProcessRequest(PeerConnection* peer)
{
    if (peer == NULL)
    {
        Debug::notifyError("ProcessRequest: peer is NULL");
        return;
    }
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
        std::cout << "RemoteWrite: " << (char*)send_base << " " << (char*)send
                  << std::endl;
        rdmasocket_->RemoteWrite(peer, (uint64_t)send_base, 0,
                                 sizeof(temp) + get_pool_data.size_);
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
            ; //直到收到数据
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
        std::cout << "Send Success";
    }
    return true;
}
