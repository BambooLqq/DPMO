#ifndef _GLOBAL_H
#define _GLOBAL_H

#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

#define MAX_CLIENT_NUM 4

// used for client or server's worker thread
// 队列实现 保证多线程的原子性和并发性

// 任务队列
template<typename T>
class Queue
{
private:
    std::vector<T> queue;
    std::mutex m;
    std::condition_variable cond;
    uint8_t offset = 0;

public:
    Queue()
    {
    }

    ~Queue()
    {
    }

    T pop()
    {
        // 同时只有一个线程pop
        std::unique_lock<std::mutex> mlock(m); // 加锁
        while (queue.empty())
        {
            cond.wait(mlock);
        }
        auto item = queue.front();
        queue.erase(queue.begin());
        return item;
    }

    void push(T item)
    {
        std::unique_lock<std::mutex> mlock(m);
        queue.push_back(item);
        mlock.unlock();
        cond.notify_one();
    }

    // 可以多线程 且不会阻塞

    T PopPolling()
    {
        while (offset == 0)
            ;
        auto item = queue.front();
        queue.erase(queue.begin());
        __sync_fetch_and_sub(&offset, 1);
        return item;
    }

    void PushPolling(T item)
    {
        queue.push_back(item);
        __sync_fetch_and_add(&offset, 1);
    }

    uint8_t size()
    {
        return __sync_fetch_and_add(&offset, 0);
    }
};

typedef enum
{
    NEWPOOL,
    GETPOOL,
    DELETEPOOL,

} Message;

#endif // !_GLOBAL_H