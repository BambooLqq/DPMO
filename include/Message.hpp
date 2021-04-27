#ifndef _MESSAGE_H_
#define _MESSAGE_H_

#include "RdmaSocket.hpp"

typedef enum
{
    CREATEPOOL,
    FINDPOOL,
    DELETEPOOL,
} Message;

typedef enum
{
    SUCCESS,
    FAIL,
    INFO,
} Status;

struct Request
{
    Message type_;
};
struct CreatePool : Request
{
    uint64_t pool_id_;
    uint16_t node_id;
    uint64_t virtual_addr_;
};

struct FindPool : Request
{
    uint64_t pool_id_;
};

struct DeletePool : Request
{
    uint64_t pool_id_;
};

struct Response
{
    Status op_ret_;
};

struct FindResponse : Response
{
    uint16_t node_id_;
    char ip_[20];
    uint64_t virtual_addr_;
};

#endif // !_MESSAGE_H_
