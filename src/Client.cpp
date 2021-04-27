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
        Debug::notifyInfo("Client Alloc Memory size %d at addr: %p successd", buf_size_, addr_);
        memset((char*)addr_, 0, buf_size_);
    }
    rdmasocket_ = new RdmaSocket(addr_, buf_size_, conf_, false, 0, sock_port, device_name, rdma_port); // RC
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

    rdmasocket_->RdmaListen();
}

Client::~Client()
{
    is_running_ = false;
    Debug::notifyInfo("Stop RPCClient.");
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
    Debug::notifyInfo("RPCClient is closed successfully.");
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
        Debug::notifyInfo("RdmaConnectServer: Have Connected With Server");
        return true;
    }
    sock = rdmasocket_->SocketConnect(node_id);
    if (sock < 0)
    {
        Debug::notifyError("Socket connection failed to server 1");
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
            Debug::notifyInfo("Client %d Connect Server %d", peer->node_id,
                              my_node_id_);
            /* Rdma Receive in Advance. */
            // 接收recv请求
            for (int i = 0; i < QPS_MAX_DEPTH; i++)
            {
                // rdmasocket_->RdmaRecv(peer->qp[0], GetClientRecvBaseAddr(peer->node_id), FOURMB);
            }
            // std::thread* poll_cq_ = new std::thread(&Server::ProcessRequest, this, peer->node_id);
            // poll_request[peer->node_id] = poll_cq_;
            Debug::debugItem("Accepted to Node%d", peer->node_id);
        }
    }
}

void Client::Listen()
{
    int sock_ = rdmasocket_->RdmaListen();
}

bool Client::SendMessageToServer()
{
    char buf[128];
    struct ibv_wc wc[1];
    const char hello[20] = "Hello Server";
    memcpy(buf, hello, sizeof(hello));
    memcpy((void*)GetServerSendBaseAddr(), buf, sizeof(buf));

    rdmasocket_->RdmaSend(peers[0]->qp[0], GetServerSendBaseAddr(), sizeof(buf));
    if (rdmasocket_->PollCompletion(peers[0]->cq, 1, wc))
    {
        std::cout << "Send Success";
    }
}