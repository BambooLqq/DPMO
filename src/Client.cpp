#include "Client.h"

Client::Client(int sock_port, std::string config_file_path) {
  conf_ = new Configuration(config_file_path);
  addr_ = 0;
  buf_size_ = FOURMB * (MAX_CLIENT_NUM);
  int ret = posix_memalign((void**)&addr_, PAGESIZE, buf_size_);

  if (ret == 0 || (uintptr_t)addr_ == NULL) {
    Debug::notifyError("Client Alloc Memory size %d failed", buf_size_);
  } else {
    Debug::notifyInfo("Client Alloc Memory size %d successd", buf_size_);
  }
  rdmasocket_ =
      new RdmaSocket(addr_, buf_size_, conf_, false, 0, sock_port);  // RC

  if (rdmasocket_->RdmaConnectServer()) {
    my_node_id = rdmasocket_->GetNodeId();
    Debug::notifyInfo("Client Connects server successfully, node id = %d",
                      my_node_id);
  } else {
    Debug::notifyError("Client connects server failed");
  }

  rdmasocket_->RdmaListen();  //等待其他机器连接
}

Client::~Client() {
  Debug::notifyInfo("Stop RPCClient.");
  if (conf_) {
    delete conf_;
    conf_ = NULL;
  }
  if (rdmasocket_) {
    delete rdmasocket_;
    rdmasocket_ = NULL;
  }
  if ((void*)addr_) {
    free((void*)addr_);
  }
  Debug::notifyInfo("RPCClient is closed successfully.");
}

uint64_t Client::GetClientBaseAddr(uint16_t node_id) {
  if (node_id < my_node_id) {
    return addr_ + node_id * FOURMB;
  } else if (node_id <= MAX_CLIENT_NUM) {
    return addr_ + (node_id - 1) * FOURMB;
  } else {
    Debug::notifyError("node %d > max_node_num", node_id);
    return 0;
  }
}
