#include "RdmaSocket.hpp"

PeerConnection ::~PeerConnection()
{
    if (cq)
    {
        ibv_destroy_cq(cq);
        cq = NULL;
    }
    for (int i = 0; i < QP_NUMBER; i++)
    {
        if (qp[i] != NULL)
        {
            ibv_destroy_qp(qp[i]);
            qp[i] = NULL;
        }
    }
}

RdmaSocket::RdmaSocket(uint64_t buf_addr, uint64_t buf_size,
                       Configuration* conf, bool is_server, uint8_t mode,
                       uint32_t sock_port, std::string device_name,
                       uint32_t rdma_port)
    : device_name_(device_name),
      sock_port_(sock_port),
      rdma_port_(rdma_port),
      gid_index_(0),
      ctx_(NULL),
      mr_(NULL),
      pd_(NULL),
      buf_addr_(buf_addr),
      buf_size_(buf_size),
      conf_(conf),
      max_node_id_(0),
      mode_(mode),
      is_running_(true),
      is_server_(is_server),
      is_new_client_(false),
      my_ip_("")
{
    // create completion queue
    server_count_ = conf_->getServerCount();
    client_count_ = conf->getClientCount(); // server的nodeid 默认为0
    max_node_id_ = client_count_ + 1; //当有新的client加入时 为他分配的id
    if (is_server)
    {
        char hname[128];
        struct hostent* hent;
        gethostname(hname, sizeof(hname));
        hent = gethostbyname(hname);
        my_ip_ = inet_ntoa(*(struct in_addr*)(hent->h_addr_list[0]));

        // my_node_id_ = conf->getIDbyIP(my_ip_); // default is 0
        if (my_ip_ != conf_->getServerIP())
        {
            Debug::notifyError("Your Ip is %s, config's server ip is %s",
                               my_ip_.c_str(), conf_->getServerIP().c_str());
        }
        else
        {
            my_node_id_ = conf_->getServerNodeID();
            Debug::notifyInfo("IP = %s, NodeID = %d", my_ip_.c_str(),
                              my_node_id_);
        }
    }
    else
    { // client
        char hname[128];
        struct hostent* hent;
        gethostname(hname, sizeof(hname));
        hent = gethostbyname(hname);
        my_ip_ = inet_ntoa(*(struct in_addr*)(hent->h_addr_list[0]));
        my_node_id_ = conf->getIDbyIP(my_ip_);

        // new client‘s ID will be assigned by server on connected
        if (my_node_id_ == -1)
        {
            Debug::notifyInfo("IP = %s , is a new client!", my_ip_.c_str());
            is_new_client_ = true;
        }
        else
        {
            Debug::notifyInfo("IP = %s, NodeID = %d", my_ip_.c_str(),
                              my_node_id_);
        }
    }
    CreateSource();

    if (!is_server_) // client
    {
        for (int i = 0; i < WORKER_NUMBER; i++)
        {
            worker_[i] = std::thread(&RdmaSocket::DataTransferWorker, this, i);
        }
    }
}

RdmaSocket::~RdmaSocket()
{
    Debug::notifyInfo("Stop RdmaSocket.");
    is_running_ = false;
    if (!is_server_)
    {
        for (int i = 0; !is_server_ && i < WORKER_NUMBER; i++)
        {
            worker_[i].detach();
        }
    }
    DestroySource();
    Debug::notifyInfo("RdmaSocket is closed successfully.");
}

// CreateResouces

bool RdmaSocket::CreateSource()
{
    /* Open device, create PD */

    ibv_device** device_list = NULL;
    ibv_device* dev = NULL;
    int rc = 0, mr_flags, device_num;
    device_list = ibv_get_device_list(&device_num);
    if (device_list == NULL)
    {
        Debug::notifyError("failed to get IB devices list");
        rc = 1;
        goto create_source_exit;
    }
    /* if there isn't any IB device in host */

    if (device_num == 0)
    {
        Debug::notifyInfo("found %d device(s)", device_num);
        rc = 1;
        goto create_source_exit;
    }
    Debug::notifyInfo("Open IB Device");
    /* search for the specific device we want to work with */

    for (int i = 0; i < device_num; i++)
    {
        if (device_name_.length() == 0)
        {
            device_name_ = ibv_get_device_name(device_list[i]);
            dev = device_list[i];
            break;
        }
        else if (device_name_ == ibv_get_device_name(device_list[i]))
        {
            dev = device_list[i];
            break;
        }
    }
    /* if the device wasn't found in host */

    if (dev == NULL)
    {
        Debug::notifyError("IB device wasn't found");
        rc = 1;
        goto create_source_exit;
    }

    ctx_ = ibv_open_device(dev);
    if (ctx_ == NULL)
    {
        Debug::notifyError("failed to open device");
        rc = 1;
        goto create_source_exit;
    }

    ibv_free_device_list(device_list);
    device_list = NULL;
    dev = NULL;

    /* query port properties */
    if (ibv_query_port(ctx_, rdma_port_, &port_attribute_))
    {
        Debug::notifyError("ibv_query_port failed");
        rc = 1;
        goto create_source_exit;
    }

    Debug::notifyInfo("Create Completion Queue");

    /* allocate Protection Domain */
    Debug::notifyInfo("Allocate Protection Domain");
    pd_ = ibv_alloc_pd(ctx_);
    if (pd_ == NULL)
    {
        Debug::notifyError("ibv_alloc_pd failed");
        rc = 1;
        goto create_source_exit;
    }

    mr_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ
               | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;

    /* register the memory buffer */
    Debug::notifyInfo("Register Memory Region");
    // std::cout << "buf_size = " << buf_size_
    //           << " buf_addr_ = " << (void*)buf_addr_ << std::endl;

    mr_ = ibv_reg_mr(pd_, (void*)buf_addr_, (size_t)buf_size_, mr_flags);
    if (mr_ == NULL)
    {
        Debug::notifyError("Memory registration failed");
        rc = 1;
        goto create_source_exit;
    }
create_source_exit:

    if (rc)
    {
        Debug::notifyError("Error Encountered, Cleanup ...");

        // clean cq

        if (pd_ != NULL)
        {
            ibv_dealloc_pd(pd_);
            pd_ = NULL;
        }
        if (ctx_)
        {
            ibv_close_device(ctx_);
            ctx_ = NULL;
        }

        if (device_list != NULL)
        {
            ibv_free_device_list(device_list);
            device_list = NULL;
        }
        return false;
    }
    return true;
}

bool RdmaSocket::ModifyQPtoInit(ibv_qp* qp)
{
    if (qp == NULL)
    {
        Debug::notifyError("Bad QP, Return");
    }
    ibv_qp_attr attr;
    int flags, rc;
    memset(&attr, 0, sizeof(ibv_qp_attr));
    flags
        = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;

    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = rdma_port_;
    attr.pkey_index = 0;
    if (mode_ == 0)
    {
        attr.qp_access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE
                               | IBV_ACCESS_REMOTE_ATOMIC
                               | IBV_ACCESS_LOCAL_WRITE;
    }
    else if (mode_ == 1)
    {
        attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;
    }
    rc = ibv_modify_qp(qp, &attr, flags);

    if (rc)
    {
        Debug::notifyError("Failed to modify QP state to INIT");
        return false;
    }
    return true;
}

bool RdmaSocket::DestroySource()
{
    bool rc = true;
    if (mr_)
    {
        if (ibv_dereg_mr(mr_))
        {
            Debug::notifyError("Failed to deregister MR");
            rc = false;
        }
    }

    if (pd_)
    {
        if (ibv_dealloc_pd(pd_))
        {
            Debug::notifyError("Failed to deallocate PD");
            printf("%s\n", strerror(errno));
            rc = false;
        }
    }

    if (ctx_)
    {
        if (ibv_close_device(ctx_))
        {
            Debug::notifyError("failed to close device context");
            rc = false;
        }
    }

    return rc;
}

bool RdmaSocket::ModifyQPtoRTR(struct ibv_qp* qp, uint32_t remote_qpn,
                               uint16_t dlid, uint8_t* dgid)
{
    struct ibv_qp_attr attr;
    int flags, rc;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IB_MTU;
    attr.dest_qp_num = remote_qpn;
    attr.rq_psn = 3185;
    attr.ah_attr.is_global = 0;
    attr.ah_attr.dlid = dlid;
    attr.ah_attr.sl = IB_SL;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = rdma_port_;
    flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN
            | IBV_QP_RQ_PSN;
    if (mode_ == 0)
    {
        attr.max_dest_rd_atomic = 16;
        attr.min_rnr_timer = 12;
        flags |= IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    }
    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc)
    {
        Debug::notifyError("failed to modify QP state to RTR");
        return false;
    }
    return true;
}

bool RdmaSocket::ModifyQPtoRTS(struct ibv_qp* qp)
{
    struct ibv_qp_attr attr;
    int flags;
    int rc;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = 3185;
    flags = IBV_QP_STATE | IBV_QP_SQ_PSN;

    if (mode_ == 0)
    {
        attr.timeout = 14;
        attr.retry_cnt = 7;
        attr.rnr_retry = 7;
        attr.max_rd_atomic = 16;
        attr.max_dest_rd_atomic = 16;
        flags |= IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY
                 | IBV_QP_MAX_QP_RD_ATOMIC;
    }
    // attr.max_rd_atomic = 1;
    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc)
    {
        Debug::notifyError("failed to modify QP state to RTS");
        return false;
    }
    return true;
}

// server <------> client
// connect
int RdmaSocket::SocketConnect(uint16_t node_id)
{
    struct sockaddr_in remote_address;
    int sock;
    struct timeval timeout = {3, 0};
    memset(&remote_address, 0, sizeof(remote_address));
    remote_address.sin_family = AF_INET;
    std::string server_ip
        = node_id == 0 ? conf_->getServerIP() : conf_->getIPbyID(node_id);
    // std::cout << "server_ip is " << server_ip << std::endl;

    inet_aton(server_ip.c_str(), (struct in_addr*)&remote_address.sin_addr);
    // std::cout << "connect port: " << sock_port_ << std::endl;

    remote_address.sin_port = htons(sock_port_);
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        Debug::notifyError("Socket Creation Failed");
        return -1;
    }
    int ret = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout,
                         sizeof(timeout));
    if (ret < 0) Debug::notifyError("Set timeout failed!");

    int t = 3;
    while (t >= 0
           && connect(sock, (struct sockaddr*)&remote_address,
                      sizeof(struct sockaddr))
                  < 0)
    {
        Debug::notifyError("Fail to connect to the server");
        t -= 1;
        usleep(1000000);
    }
    if (t < 0)
    {
        return -1;
    }
    return sock;
}

int RdmaSocket::SockSyncData(int sock, int size, char* local_data,
                             char* remote_data)
{
    int rc;
    int readBytes = 0;
    int totalReadBytes = 0;
    rc = write(sock, local_data, size);
    if (rc < size)
    {
        Debug::notifyError("Failed writing data during sock_sync_data");
    }
    else
    {
        rc = 0;
    }
    while (!rc && totalReadBytes < size)
    {
        readBytes = read(sock, remote_data, size);
        if (readBytes > 0)
        {
            totalReadBytes += readBytes;
        }
        else
        {
            rc = readBytes;
        }
    }
    return rc;
}

int RdmaSocket::RdmaListen()
{
    struct sockaddr_in my_address;
    int sock;
    int on = 1;
    /* Socket Initialization */
    memset(&my_address, 0, sizeof(sockaddr_in));
    my_address.sin_family = AF_INET;
    my_address.sin_addr.s_addr = INADDR_ANY;
    // just for test
    //只有两台机器 Server和Client不监听同一端口
    if (!is_server_)
    {
        sock_port_ += 1;
    }
    // std::cout << "listen port: " << sock_port_ << std::endl;
    my_address.sin_port = htons(sock_port_);

    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        Debug::debugItem("Socket creation failed");
    }

    if ((setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)
    {
        Debug::debugItem("Setsockopt failed");
    }

    if (bind(sock, (struct sockaddr*)&my_address, sizeof(struct sockaddr)) < 0)
    {
        Debug::debugItem("Bind failed with errnum ", errno);
    }

    listen(sock, 5);
    return sock;
}

bool RdmaSocket::ConnectQueuePair(PeerConnection* peer)
{
    ExchangeID *local_id = new ExchangeID(), *remote_id = new ExchangeID();
    ExchangeRdmaMeta local_metadata, remote_metata;

    int rc = 0;
    bool ret = true;
    union ibv_gid my_gid;

    // 交换ID 主要目的是
    if (is_server_)
    {
        local_id->is_server_or_new_client = 0;
        local_id->node_id = my_node_id_;
        memcpy(local_id->peer_ip, my_ip_.c_str(), my_ip_.size());
        local_id->given_id = max_node_id_;
    }
    else if (is_new_client_ == false)
    {
        local_id->is_server_or_new_client = 1;
        local_id->node_id = my_node_id_;
        // local_id->peer_ip = my_ip_;
        memcpy(local_id->peer_ip, my_ip_.c_str(), my_ip_.size());
        local_id->given_id = 0;
    }
    else if (is_new_client_)
    {
        local_id->is_server_or_new_client = 2;
        // local_id->peer_ip = my_ip_;
        memcpy(local_id->peer_ip, my_ip_.c_str(), my_ip_.size());
    }

    if (SockSyncData(peer->sock, sizeof(ExchangeID), (char*)local_id,
                     (char*)remote_id)
        < 0)
    {
        Debug::notifyError("failed to exchange connection data between sides");
        rc = 1;
        goto ConnectQPExit;
    }
    // peer->peer_ip = remote_id->peer_ip;
    memcpy(peer->peer_ip, remote_id->peer_ip, sizeof(remote_id->peer_ip));

    if (is_server_)
    {
        if (remote_id->is_server_or_new_client == 1)
        {
            peer->node_id = remote_id->node_id;
        }
        else if (remote_id->is_server_or_new_client == 2)
        {
            peer->node_id = max_node_id_;
            max_node_id_++;
            client_count_++;
            conf_->addClient(peer->node_id, peer->peer_ip);
        }
    }
    else if (is_new_client_ && remote_id->is_server_or_new_client == 0)
    {
        my_node_id_ = remote_id->given_id;
        peer->node_id = remote_id->node_id;
        is_new_client_ = false;
        client_count_++;
        conf_->addClient(my_node_id_, my_ip_);
    }
    else if (is_new_client_ == false)
    {
        peer->node_id = remote_id->node_id;
    }

    CreateQueuePair(peer);

    if (gid_index_ >= 0)
    {
        rc = ibv_query_gid(ctx_, rdma_port_, gid_index_, &my_gid);
        if (rc)
        {
            Debug::notifyError("could not get gid for port: %d, index: %d",
                               rdma_port_, gid_index_);
            return false;
        }
    }
    else
    {
        memset(&my_gid, 0, sizeof(my_gid));
    }

    // 交换控制信息
    peer->my_buf_addr_ = GetPeerAddr(peer->node_id);
    local_metadata.rkey = mr_->rkey;
    for (int i = 0; i < (is_server_ ? 1 : QP_NUMBER); i++)
    {
        local_metadata.qp_num[i] = peer->qp[i]->qp_num;
    }
    local_metadata.lid = port_attribute_.lid;
    local_metadata.buf_addr = peer->my_buf_addr_ + FOURMB;
    memcpy(local_metadata.gid, &my_gid, 16);
    if (SockSyncData(peer->sock, sizeof(ExchangeRdmaMeta),
                     (char*)&local_metadata, (char*)&remote_metata)
        < 0)
    {
        Debug::notifyError("failed to exchange connection data between sides");
        rc = 1;
        goto ConnectQPExit;
    }

    peer->rkey = remote_metata.rkey;
    peer->lid = remote_metata.lid;
    peer->peer_buf_addr_ = remote_metata.buf_addr;
    memcpy(peer->gid, remote_metata.gid, 16);
    for (int i = 0;
         i < (remote_id->is_server_or_new_client == 0 ? 1 : QP_NUMBER); i++)
    {
        peer->qp_num[i] = remote_metata.qp_num[i];
    }

    // 更换QP状态
    for (int i = 0; i < (is_server_ ? 1 : QP_NUMBER); i++)
    {
        /* modify the QP to init */
        ret = ModifyQPtoInit(peer->qp[i]);
        if (ret == false)
        {
            Debug::notifyError("change QP[%d] state to INIT failed", i);
            rc = 1;
            goto ConnectQPExit;
        }
        /* modify the QP to RTR */
        ret = ModifyQPtoRTR(peer->qp[i], peer->qp_num[i], peer->lid, peer->gid);
        if (ret == false)
        {
            Debug::notifyError("failed to modify QP[%d] state to RTR", i);
            rc = 1;
            goto ConnectQPExit;
        }
        /* Modify the QP to RTS */
        ret = ModifyQPtoRTS(peer->qp[i]);
        if (ret == false)
        {
            Debug::notifyError("failed to modify QP[%d] state to RTR", i);
            rc = 1;
            goto ConnectQPExit;
        }
    }

ConnectQPExit:
    delete local_id;
    delete remote_id;
    if (rc == 1)
    {
        return false;
    }
    else
    {
        return true;
    }
}

bool RdmaSocket::CreateQueuePair(PeerConnection* peer)
{
    struct ibv_qp_init_attr attr;
    memset(&attr, 0, sizeof(attr));

    if (mode_ == 0)
    {
        attr.qp_type = IBV_QPT_RC;
    }
    else if (mode_ == 1)
    {
        attr.qp_type = IBV_QPT_UC;
    }
    attr.sq_sig_all = 0;
    peer->cq = ibv_create_cq(ctx_, QPS_MAX_DEPTH, NULL, NULL, 0);
    if (peer->cq == NULL)
    {
        Debug::notifyError("failed to create CQ");
        return false;
    }

    attr.send_cq = peer->cq;
    attr.recv_cq = peer->cq;

    attr.cap.max_send_wr = QPS_MAX_DEPTH;
    attr.cap.max_recv_wr = QPS_MAX_DEPTH;
    attr.cap.max_send_sge = 1;
    attr.cap.max_recv_sge = 1;
    attr.cap.max_inline_data = 0;

    for (int i = 0; i < (is_server_ ? 1 : QP_NUMBER); i++)
    {
        peer->qp[i] = ibv_create_qp(pd_, &attr);
        Debug::notifyInfo("Create Queue Pair with Num = %d",
                          peer->qp[i]->qp_num);
        if (!peer->qp[i])
        {
            Debug::notifyError("Failed to create QP");
            return false;
        }
    }

    return true;
}

int RdmaSocket::PollCompletion(struct ibv_cq* cq, int poll_number,
                               struct ibv_wc* wc)
{
    if (cq == NULL)
    {
        Debug::notifyError("PollCompletion: cq = NULL");
        return -1;
    }

    int count = 0;
    do
    {
        count += ibv_poll_cq(cq, 1, wc);
    } while (count < poll_number);

    if (count < 0)
    {
        Debug::notifyError("Poll Completion failed.");
        return -1;
    }

    /* Check Completion Status */
    if (wc->status != IBV_WC_SUCCESS)
    {
        Debug::notifyError("Failed status %s (%d) for wr_id %d",
                           ibv_wc_status_str(wc->status), wc->status,
                           (int)wc->wr_id);
        return -1;
    }
    Debug::debugItem("Find New Completion Message");
    return count;
}

int RdmaSocket::PollCompletionOnce(struct ibv_cq* cq, int poll_number,
                                   struct ibv_wc* wc)
{
    if (cq == NULL)
    {
        Debug::notifyError("PollCompletionOnce: cq = NULL");
        return -1;
    }
    int count = ibv_poll_cq(cq, poll_number, wc);
    if (count == 0)
    {
        return 0;
    }
    else if (count == -1)
    {
        Debug::notifyError(
            "Failure occurred when reading work completions, ret = %d", count);
        return -1;
    }

    if (wc->status != IBV_WC_SUCCESS)
    {
        Debug::notifyError("Failed status %s (%d) for wr_id %d",
                           ibv_wc_status_str(wc->status), wc->status,
                           (int)wc->wr_id);
        return -1;
    }
    else
    {
        return count;
    }
}

bool RdmaSocket::RdmaSend(struct ibv_qp* qp, uint64_t source_buffer,
                          uint64_t buffer_size)
{
    if (qp == NULL)
    {
        Debug::notifyError("RdmaSend: QP = NULL");
        return false;
    }
    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr* wrBad;

    memset(&sg, 0, sizeof(sg));
    sg.addr = (uintptr_t)source_buffer;
    sg.length = buffer_size;
    sg.lkey = mr_->lkey;

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 0;
    wr.sg_list = &sg;
    wr.num_sge = 1;
    wr.imm_data = (uint32_t)my_node_id_;
    wr.opcode = IBV_WR_SEND_WITH_IMM;
    wr.send_flags = IBV_SEND_SIGNALED;

    if (ibv_post_send(qp, &wr, &wrBad))
    {
        Debug::notifyError("Send with RDMA_SEND failed.");
        return false;
    }
    return true;
}

bool RdmaSocket::RdmaRecv(struct ibv_qp* qp, uint64_t source_buffer,
                          uint64_t buffer_size)
{
    if (qp == NULL)
    {
        Debug::notifyError("RdmaRecv: QP = NULL");
        return false;
    }
    struct ibv_sge sg;
    struct ibv_recv_wr wr;
    struct ibv_recv_wr* wrBad;
    int ret;
    memset(&sg, 0, sizeof(sg));
    sg.addr = (uintptr_t)source_buffer;
    sg.length = buffer_size;
    sg.lkey = mr_->lkey;

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 0;
    wr.sg_list = &sg;
    wr.num_sge = 1;
    ret = ibv_post_recv(qp, &wr, &wrBad);
    if (ret)
    {
        Debug::notifyError("Receive with RDMA_RECV failed, ret = %d.", ret);
        return false;
    }
    return true;
}

bool RdmaSocket::RdmaWrite(PeerConnection* peer, uint64_t source_buffer,
                           uint64_t des_buffer, uint64_t buffer_size,
                           uint32_t imm, int worker_id)
{
    if (peer == NULL)
    {
        Debug::notifyError("RDMAWrite peer is null");
        return false;
    }
    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr* wrBad;
    memset(&sg, 0, sizeof(sg));
    sg.addr = (uintptr_t)source_buffer;
    sg.length = buffer_size;
    sg.lkey = mr_->lkey;

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 0;
    wr.sg_list = &sg;
    wr.num_sge = 1;
    if ((int32_t)imm == -1)
    {
        wr.opcode = IBV_WR_RDMA_WRITE;
    }
    else
    {
        wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        wr.imm_data = imm;
    }
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = des_buffer + peer->peer_buf_addr_;
    Debug::debugItem("Post RDMA_WRITE with remote address = %lx",
                     wr.wr.rdma.remote_addr);
    wr.wr.rdma.rkey = peer->rkey;
    if (ibv_post_send(peer->qp[worker_id], &wr, &wrBad))
    {
        Debug::notifyError("Send with RDMA_WRITE(WITH_IMM) failed.");
        printf("%s\n", strerror(errno));
        return false;
    }
    return true;
}

bool RdmaSocket::OutboundHamal(PeerConnection* peer, uint64_t buffer_send,
                               uint64_t recv_offset, uint64_t size,
                               int worker_id)
{
    if (peer == NULL)
    {
        Debug::notifyError("OutboundHamal peer is null");
        return false;
    }
    uint64_t send_packet_size = ONEMB;
    uint64_t sendaddr = peer->my_buf_addr_ + worker_id * ONEMB;
    uint64_t total_size = 0;
    uint64_t send_size;

    struct ibv_wc wc;
    while (total_size < size)
    {
        send_size = (size - total_size) >= send_packet_size
                        ? send_packet_size
                        : ((size - total_size));

        memcpy((void*)sendaddr, (void*)(buffer_send + total_size), send_size);
        RdmaWrite(peer, sendaddr, recv_offset + total_size, send_size, -1,
                  worker_id);
        PollCompletion(peer->cq, 1, &wc);
        Debug::notifyInfo("Source Addr = %lx, Des Addr = %lx, Size = %d",
                          sendaddr, recv_offset + total_size, send_size);
        total_size += send_size;
    }
    __sync_fetch_and_add(&transfer_count, 1);
    return true;
}

bool RdmaSocket::RemoteWrite(PeerConnection* peer, uint64_t buffer_send,
                             uint64_t recv_offset, uint64_t size)
{
    if (peer == NULL)
    {
        Debug::notifyError("RemoteWrite peer is null");
        return false;
    }
    uint64_t ship_size = 0;

    if (size < ONEMB)
    {
        OutboundHamal(peer, buffer_send, recv_offset, size, 0);
        return true;
    }
    else
    {
        transfer_count = 0;
        ship_size = size / WORKER_NUMBER;
        ship_size = ship_size >> 12 << 12; // 4KB对齐
        for (int i = 0; i < WORKER_NUMBER; i++)
        {
            TransferTask* task = new TransferTask();
            task->op_type = WRITE; // write
            task->recv.recv_offset = recv_offset + i * ship_size;
            task->send.buffer_send_addr = buffer_send + i * ship_size;
            task->size = ship_size;
            task->peer = peer;
            queue_[i].PushPolling(task);
        }

        // 0号qp发送剩下的数据
        OutboundHamal(peer, buffer_send + WORKER_NUMBER * ship_size,
                      recv_offset + WORKER_NUMBER * ship_size,
                      size - (WORKER_NUMBER * ship_size), 0);
        while (transfer_count != WORKER_NUMBER)
            ;
        return true;
    }
}

bool RdmaSocket::RdmaRead(PeerConnection* peer, uint64_t buffer_recv,
                          uint64_t des_offset, uint64_t size, int worker_id)
{
    if (peer == NULL)
    {
        Debug::notifyError("RdmaRead peer is null");
        return false;
    }
    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr* wrBad;

    memset(&sg, 0, sizeof(sg));
    sg.addr = (uintptr_t)buffer_recv;
    sg.length = des_offset;
    sg.lkey = mr_->lkey;

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 0;
    wr.sg_list = &sg;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_READ;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = des_offset + peer->peer_buf_addr_;
    wr.wr.rdma.rkey = peer->rkey;

    if (ibv_post_send(peer->qp[worker_id], &wr, &wrBad))
    {
        Debug::notifyError("Send with RDMA_READ failed.");
        return false;
    }
    return true;
}

bool RdmaSocket::InboundHamal(PeerConnection* peer, uint64_t buffer_recv,
                              uint64_t des_offset, uint64_t size, int worker_id)
{
    if (peer == NULL)
    {
        Debug::notifyError("InboundHamal peer is null");
        return false;
    }
    uint64_t read_packet_size = ONEMB;
    uint64_t read_addr = buf_addr_ + worker_id * ONEMB;
    uint64_t total_read_size = 0, read_size;

    struct ibv_wc wc;
    while (total_read_size < size)
    {
        read_size = (size - total_read_size) >= read_packet_size
                        ? read_packet_size
                        : (size - total_read_size);

        RdmaRead(peer, read_addr, des_offset + total_read_size, read_size,
                 worker_id);
        PollCompletion(peer->cq, 1, &wc);
        memcpy((void*)(buffer_recv + total_read_size), (void*)read_addr,
               read_size);
        total_read_size += read_size;
    }
    __sync_fetch_and_add(&transfer_count, 1);
    return true;
}

bool RdmaSocket::RemoteRead(PeerConnection* peer, uint64_t buffer_recv,
                            uint64_t des_offset, uint64_t size)
{
    if (peer == NULL)
    {
        Debug::notifyError("RemoteRead peer is null");
        return false;
    }
    int ship_size;
    if (size < ONEMB)
    {
        InboundHamal(peer, buffer_recv, des_offset, size, 0);
        return true;
    }
    else
    {
        transfer_count = 0;
        ship_size = size / WORKER_NUMBER;
        ship_size = ship_size >> 12 << 12; // 4kb对齐
        for (int i = 0; i < WORKER_NUMBER; i++)
        {
            TransferTask* task = new TransferTask();
            task->peer = peer;
            task->op_type = READ;
            task->size = ship_size;
            task->send.buffer_recv = buffer_recv + i * ship_size;
            task->recv.des_offset = des_offset + i * ship_size;
            queue_[i].PushPolling(task);
        }
        InboundHamal(peer, buffer_recv + WORKER_NUMBER * ship_size,
                     des_offset + WORKER_NUMBER * ship_size,
                     size - WORKER_NUMBER * ship_size, 0);

        while (transfer_count != WORKER_NUMBER)
        {
            ;
        }
        return true;
    }
}

void RdmaSocket::DataTransferWorker(int worker_id)
{
    TransferTask* task;
    while (is_running_)
    {
        task = queue_[worker_id].PopPolling();
        if (task->op_type == WRITE)
        {
            OutboundHamal(task->peer, task->send.buffer_send_addr,
                          task->recv.recv_offset, task->size, worker_id + 1);
        }
        else if (task->op_type == READ)
        {
            InboundHamal(task->peer, task->send.buffer_recv,
                         task->recv.des_offset, task->size, worker_id + 1);
        }
        delete task;
    }
}
void RdmaSocket::RdmaQueryQueuePair(struct ibv_qp* qp)
{
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    if (qp == NULL)
    {
        Debug::notifyError("RdmaQueryQueuePair: qp = NULL");
        return;
    }
    ibv_query_qp(qp, &attr, IBV_QP_STATE, &init_attr);
    switch (attr.qp_state)
    {
    case IBV_QPS_RESET: Debug::notifyInfo("QP state: IBV_QPS_RESET\n"); break;
    case IBV_QPS_INIT: Debug::notifyInfo("QP state: IBV_QPS_INIT\n"); break;
    case IBV_QPS_RTR: Debug::notifyInfo("QP state: IBV_QPS_RTR\n"); break;
    case IBV_QPS_RTS: Debug::notifyInfo("QP state: IBV_QPS_RTS\n"); break;
    case IBV_QPS_SQD: Debug::notifyInfo("QP state: IBV_QPS_SQD\n"); break;
    case IBV_QPS_SQE: Debug::notifyInfo("QP state: IBV_QPS_SQE\n"); break;
    case IBV_QPS_ERR: Debug::notifyInfo("QP state: IBV_QPS_ERR\n"); break;
    case IBV_QPS_UNKNOWN:
        Debug::notifyInfo("QP state: state: IBV_QPS_UNKNOWN\n");
        break;
    }
    return;
}