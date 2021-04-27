#ifndef _RDMA_SOCKET_H
#define _RDMA_SOCKET_H

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <infiniband/verbs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <thread>
#include <unordered_map>

#include "Configuration.hpp"
#include "Debug.hpp"
#include "Global.hpp"

#define MAX_POST_LIST 24
#define QPS_MAX_DEPTH 128
#define SIGNAL_BATCH  31

#define WORKER_NUMBER 2
#define QP_NUMBER     (1 + WORKER_NUMBER)

#define PEER_NUMBER 1000

#define IB_MTU IBV_MTU_4096
#define IB_SL  0

#define SENDSIZE 1024 * 1024
#define ONEMB    1024 * 1024
#define FOURMB   1024 * 1024 * 4
#define FOURKB   4 * 1024 // 4KB

#define PAGESIZE sysconf(_SC_PAGESIZE)

class PeerConnection
{
public:
    // 主线程发送请求在qp[0]；
    // worker thread发送请求到qp[1 - qp_number-1]
    // 存在与不同peer连接的qp 共用一个cq的情况
    // 同一个peer的qp使用相同放入cq
    ibv_qp* qp[QP_NUMBER];
    ibv_cq* cq;

    /*
    bellow data used for rdma read / write /send /recv
    get from socket exchange metadata
   */

    uint32_t qp_num[QP_NUMBER];
    uint64_t peer_buf_addr_;
    uint64_t my_buf_addr_;
    uint32_t rkey;
    uint16_t lid;
    uint8_t gid[16];

    int sock;
    uint16_t node_id;
    char peer_ip[20];

    uint64_t counter = 0;
    ~PeerConnection();
};

class ExchangeID
{
public:
    uint16_t node_id;
    char peer_ip[20];
    // 0 - server
    // 1 - exist-client
    // 2 - new-client
    int is_server_or_new_client;
    // used for new client without node id
    // server assigns a node_id for the new client
    uint16_t given_id;
};

// used for rdma connc
typedef struct ExchangeRdmaMeta
{
    uint32_t rkey;
    uint32_t qp_num[QP_NUMBER];
    uint16_t lid;
    uint64_t buf_addr;
    uint16_t node_id;
    uint8_t gid[16];
} ExchangeRdmaMeta;

typedef enum
{
    READ,
    WRITE
} ReqType;

typedef struct TransferTask
{
    ReqType op_type;
    PeerConnection* peer;
    uint64_t size;
    union
    {
        uint64_t buffer_send_addr; // write
        uint64_t buffer_recv;      // read
    } send;
    union
    {
        uint64_t recv_offset; // write
        uint64_t des_offset;  // read
    } recv;

} TransferTask;

/* rdma connection need
    1. device ibv_get_device_list
    2. get ibv_context
    3. free devlist  // 擦屁股操作
    4. 查询rdma端口信息 ibv_query_port、ibv_port_attr
    5. 分配一个protection domain
    6. 创建一个Complete Quene ibv_alloc_pd、ibv_pd
    7. 注册Memory region ibv_reg_mr、ibv_mr
    8. 创建Quene Pair ibv_create_qp、ibv_qp
    9. 交换控制信息 使用socket 或者 RDMA_CM_API 我们使用socket
    10. 转换QP状态 RESET -> INIT -> RTR -> RTS ibv_modify_qp
        - 状态：RESET -> INIT -> RTR -> RTS
        - 要严格按照顺序进行转换
        - QP刚创建时状态为RESET
        - INIT之后就可以调用ibv_post_recv提交一个receive buffer了
        - 当 QP进入RTR(ready to receive)状态以后，便开始进行接收处理
        - RTR之后便可以转为RTS(ready to send)，RTS状态下可以调用ibv_post_send
    11. 创建 发送任务send_wr / 接收任务 recive_wr
        - ibv_send_wr（send work request）
        - 该任务会被提交到QP中的SQ（Send Queue）中
        - 发送任务有三种操作：Send,Read,Write
        - Send操作需要对方执行相应的Receive操作
        - Read/Write直接操作对方内存，对方无感知
        - 把要发送的数据的内存地址，大小，密钥告诉HCA
        - Read/Write还需要告诉HCA远程的内存地址和密钥
    12 提交发送/接收任务 ibv_post_send ibv_post_recv
    13 轮询任务完成信息 ibv_poll_cq
*/

class RdmaSocket
{
private:
    // first is nodeid
    // second is peerConnection data
    // std::unordered_map<uint16_t, PeerConnection*> peers;

    std::string device_name_;
    uint32_t sock_port_;
    uint32_t rdma_port_;
    int gid_index_;
    ibv_port_attr port_attribute_;

    ibv_context* ctx_;
    ibv_mr* mr_;
    ibv_pd* pd_;
    uint64_t buf_addr_; // buf地址
    uint64_t buf_size_; // buf size

    Configuration* conf_;

    uint16_t my_node_id_;
    uint16_t max_node_id_;

    uint8_t mode_; // RC - 0 , UC - 1, UD - 2 // 可能都是RC

    bool is_running_;
    bool is_server_;
    bool is_new_client_;

    int client_count_;
    int server_count_;

    std::string my_ip_;

    // WORKER_NUMBER个工作thread
    // 每个thread[id]不断轮询quene[id]上的工作任务并执行
    std::thread worker_[WORKER_NUMBER];
    Queue<TransferTask*> queue_[WORKER_NUMBER];
    uint16_t transfer_count;

    // function
    bool CreateSource();
    bool DestroySource();

    bool CreateQueuePair(PeerConnection* peer);

    bool ModifyQPtoInit(struct ibv_qp* qp);
    bool ModifyQPtoRTR(struct ibv_qp* qp, uint32_t remote_qpn, uint16_t dlid,
                       uint8_t* dgid);
    bool ModifyQPtoRTS(struct ibv_qp* qp);

    // sock连接nodeID号机器

    int SockSyncData(int sock, int size, char* local_data, char* remote_data);

    void DataTransferWorker(int id);

    void AddClient(uint16_t node_id, std::string ip)
    {
        conf_->addClient(node_id, ip);
    }

    uint64_t GetPeerAddr(uint16_t node_id)
    {
        if (node_id < my_node_id_)
        {
            return buf_addr_ + FOURMB * 2 * node_id;
        }
        else if (node_id <= MAX_CLIENT_NUM)
        {
            return buf_addr_ + FOURMB * 2 * (node_id - 1);
        }
        return 0;
    }

public:
    /*
    cq_num 为将要创建的cq的数量
    buf_addr 缓冲区首址
    buf_size 缓冲区大小
    socl_port 使用sock进行同步的时候 使用的端口信息
    device_name 希望使用的rdma网卡设备名 如果为空字符串将找一个可以用的
   */
    RdmaSocket(uint64_t buf_addr, uint64_t buf_size, Configuration* conf,
               bool is_server, uint8_t mode, uint32_t sock_port = 0,
               std::string device_name = "", uint32_t rdma_port = 1);
    ~RdmaSocket();

    int SocketConnect(uint16_t NodeID);

    // 等待连接
    int RdmaListen();

    //
    bool ConnectQueuePair(PeerConnection* peer);
    int PollCompletion(uint16_t node_id, int poll_number, struct ibv_wc* wc);

    uint16_t GetNodeId()
    {
        return my_node_id_;
    }

    // 拉取第poll_number个wc，
    int PollCompletion(struct ibv_cq* cq, int poll_number, struct ibv_wc* wc);

    // 拉取poll_Number个wc 放在wc数组中
    int PollCompletionOnce(struct ibv_cq* cq, int poll_number,
                           struct ibv_wc* wc);

    // nodeid 发送到nodeid
    // source buffer keep sending data
    // buffer_size sending data size
    // source_buffer 为地址
    bool RdmaSend(struct ibv_qp* qp, uint64_t source_buffer,
                  uint64_t buffer_size);

    // nodeid 发送到nodeid
    // source buffer keep receving data
    // buffer_size recving data size
    // source_buffer 为地址
    bool RdmaRecv(struct ibv_qp* qp, uint64_t source_buffer,
                  uint64_t buffer_size);

    // source buffer为发送数据的绝对地址
    // des_buffer 为要写入的相对地址
    // imm -1 为 RDMA_WRITE
    // read write 用于Client间读取数据

    bool RdmaWrite(PeerConnection* peer, uint64_t buffer_send,
                   uint64_t recv_offset, uint64_t size, uint32_t imm,
                   int worker_id);

    bool OutboundHamal(PeerConnection* peer, uint64_t buffer_send,
                       uint64_t recv_offset, uint64_t size, int worker_id);
    bool RemoteWrite(PeerConnection* peer, uint64_t buffer_send,
                     uint64_t recv_offset, uint64_t size);

    // buffer_recv 为接收数据的绝对地址
    // des_buffer 为读取数据的相对地址
    // size读取数据的大小
    bool RdmaRead(PeerConnection* peer, uint64_t buffer_recv,
                  uint64_t des_offset, uint64_t size, int worker_id);
    bool InboundHamal(PeerConnection* peer, uint64_t buffer_recv,
                      uint64_t des_offset, uint64_t size, int worker_id);
    bool RemoteRead(PeerConnection* peer, uint64_t buffer_recv,
                    uint64_t des_offset, uint64_t size);

    void RdmaQueryQueuePair(struct ibv_qp* qp);
};

#endif // !_RDMA_SOCKET_H