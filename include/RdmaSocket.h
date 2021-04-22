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

#include "Configuration.h"
#include "Debug.h"
#include "Global.h"

#define MAX_POST_LIST 24
#define QPS_MAX_DEPTH 128
#define SIGNAL_BATCH 31
#define WORKER_NUMBER 2
#define QP_NUMBER (1 + WORKER_NUMBER)
#define PEER_NUMBER 1000

typedef struct PeerConnection {
  ibv_qp *qp[QP_NUMBER];
  ibv_cq *cq;

  /*
    bellow data used for rdma read / write /send /recv
    get from socket exchange metadata
   */

  uint32_t qp_num[QP_NUMBER];
  uint64_t buf_addr;
  uint32_t rkey;
  uint16_t lid;
  uint8_t gid[16];

  int sock;
  uint16_t node_id;
  std::string peer_ip;

  uint64_t counter = 0;
} PeerConnection;

typedef struct ExchangeID {
  uint16_t node_id;
  std::string peer_ip;
  bool is_server;

  // used for new client without node id
  // server assigns a node_id for the new client
  uint16_t given_id;

} ExchangeID;

// used for rdma connc
typedef struct ExchangeRdmaMeta {
  uint32_t rkey;
  uint32_t qp_num[QP_NUMBER];
  uint16_t lid;
  uint64_t buf_addr;
  uint16_t node_id;
  uint8_t gid[16];
} ExchangeRdmaMeta;

typedef struct TransferTask {
  bool op_type; /* false - Read, true - Write */
  uint16_t node_id;
  uint64_t size;
  uint64_t buffer_send_addr;
  uint64_t buffer_receive_addr;
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

class RdmaSocket {
 private:
  PeerConnection *peers_[PEER_NUMBER];  // connect with other client or server
  char *device_name_;
  uint32_t sock_port_;
  uint32_t rdma_port_;
  int git_index_;
  ibv_port_attr port_attribute_;
  bool is_running_;
  bool is_server_;
  ibv_context *ctx_;
  ibv_pd *pd_;
  ibv_cq **cq_;
  ibv_mr *mr_;
  int cq_num_;
  int cq_ptr_;
  Configuration *conf_;
  uint16_t my_node_id_;
  uint16_t max_node_id_;
  std::thread listener_;  // wait for client / server connection
  uint8_t mode_;          // RC - 0 , UC - 1, UD - 2 // 可能都是RC

  int client_count_;
  int server_count_;

  // WORKER_NUMBER个工作thread
  // 每个thread[id]不断轮询quene[id]上的工作任务并执行
  std::thread worker_[WORKER_NUMBER];
  Queue<TransferTask *> queue_[WORKER_NUMBER];

  // function
  bool CreateSource();

 public:
  RdmaSocket();
};

#endif  // !_RDMA_SOCKET_H