#include "Server.h"

Server::Server(int sock_port = 0, std::string config_file_path = "") {
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
      new RdmaSocket(addr_, buf_size_, conf_, true, 0, sock_port);  // RC

  rdmasocket_->RdmaListen();
}

Server::~Server() {
  Debug::notifyInfo("Stop RPCServer.");
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

  if (pool_info_.size()) {
    for (auto itr = pool_info_.begin(); itr != pool_info_.end(); itr++) {
      delete itr->second;
    }
    pool_info_.clear();
  }
  Debug::notifyInfo("RPCServer is closed successfully.");
}

bool Server::AddPool(uint32_t pool_id, uint16_t node_id, uint64_t va) {
  if (pool_info_.find(pool_id) != pool_info_.end()) {
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

PoolInfo* Server::GetPool(uint32_t pool_id) {
  if (pool_info_.find(pool_id) != pool_info_.end()) {
    return pool_info_.find(pool_id)->second;
  }
  Debug::notifyError("Don't exist the pool %d", pool_id);
  return NULL;
}

bool Server::DeletePool(uint32_t pool_id) {
  if (pool_info_.find(pool_id) != pool_info_.end()) {
    pool_info_.erase(pool_id);
    return true;
  }
  Debug::notifyError("Don't exist the pool %d", pool_id);
  return false;
}