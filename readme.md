# DPMO

an RDMA-enabled distributed Persistent Memory Object System

## Build
- mkdir build && cd build && cmake .. && make server
- ./bin/server
- cd ../test && mkdir build && cd build && cmake .. && make -j32

### RDMA Write to persistent Memory domain
  
当使用RDMA write的时候，一般会直写到CPU的L3 cache，为了直写到Persistent Memory
我们使用RDMA WRITE的时候 记录下<addr, length >对，当Write完成后，发送RDMA Send请求 请求包含这些<addr, length>对，远端收到请求后 将对应addr的数据刷写到PM
中

### 现存的问题

1. 当双方建立连接的时候，MR的已经Create，即缓冲区已经创建完成，当需要读取/写入池数据的时候，需要将池的数据复制到缓冲区，然后发送到请求方(也可以使用Write写入发送方)

2. 内存使用问题 

- RDMA的缓冲区需要和页大小对齐
- 缓冲区给每个线程使用的时候，需要考虑好大小

3. RDMASocket仍然存在一些问题 需要进行优化

4. 使用多线程或者线程池对请求进行处理 
- 目前使用多线程，后续优化为线程池

### 备注
1. 连接双方通信包括
- Send/Recv 消息机制 可以用来传输请求
- Read/Recv 内存读写 用来传输数据


### 2021-5-1
1. 考虑RDMA Write时，刷写至持久内存中
2. 考虑RDMA Read时，将数据直接读入持久内存中

### 下一步
- 实现远程创建池和对象
