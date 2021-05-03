#ifndef _MESSAGE_H_
#define _MESSAGE_H_

#include "RdmaSocket.hpp"

typedef enum
{
    CREATEPOOL,
    FINDPOOL,
    DELETEPOOL,
    GETPOOLDATA,
    WRITEPOOLDATA,
    CREATEREMOTEPOOL,
    CLOSEREMOTEPOOL,
    OPENREMOTEPOOL,
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

struct GetPoolData : Request
{
    uint64_t virtual_address_;
    uint64_t offset_;
    size_t size_;
};

struct WritePoolData : Request
{
    uint64_t virtual_address_;
    uint64_t offset_;
    size_t size_;
};

struct CreateRemotePool : Request
{
    char path[128];
    char layout[128];
    size_t poolsize;
    mode_t mode;
};

struct CloseRemotePool : Request
{
    uint64_t pool_id_;
};

struct OpenRemotePool : Request
{
    char path[128];
    char layout[128];
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

struct CreateRemotePoolResponse : Response
{
    uint64_t pool_id_;
};

struct OpenRemotePoolResponse : Response
{
    uint64_t pool_id_;
};
#endif // !_MESSAGE_H_
