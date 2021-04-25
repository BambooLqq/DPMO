#ifndef _CLIENT_H
#define _CLIENT_H

#include "RdmaSocket.h"

// for client's mem
// 为每个连接分配4KB的缓冲区
// ***********************************************************************************************
// 	+-------+-------+-----+-------+-----+-------+-------+-----+-------+-
// 	| Server | Cli_1 | ... | Cli_N-1 || Client N+1 || Client N+2 || ...
// 	+-------+-------+-----+-------+-----+-------+-------+-----+-------+-

class Client {
  Configuration* conf_;
  RdmaSocket* rdmasocket_;
  uint64_t addr_;      // mr_addr
  uint64_t buf_size_;  // default (node + 1) * 4MB
  uint16_t my_node_id;

 public:
  Client(int sock_port = 0, std::string config_file_path = "");
  ~Client();
  uint64_t GetServerBaseAddr() { return addr_; }
  uint64_t GetClientBaseAddr(uint16_t node_id);
};

#endif  // !_CLIENT_H