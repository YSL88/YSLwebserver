#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理
    bool m_stop;                //是否结束线程
    connection_pool *m_connPool;  //数据库连接池
};
template <typename T>
threadpool<T>::threadpool( connection_pool *connPool, int thread_number, int max_requests) : m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    m_threads = new pthread_t[m_thread_number];
    // 创建具体的线程池，表现形式为数组，里面的东西是线程ID，pthread_t 当成 ID 就行，可以根据ID识别具体线程
    // pthread_t 抽象数据类型,用于在线程之间传递线程ID信息。pthread_t类型的变量可以用来引用某个具体的线程。
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)
    {
        //printf("create the %dth thread\n",i);
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)  // pthread_create 根据线程ID 创建线程
        {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i]))
        // pthread_detach()函数可以将一个线程从可连接(joinable)状态转换为分离(detached)状态。
        /*
         * **可连接状态**
            - 子线程执行结束,但资源不会被回收
            - 父线程调用pthread_join()获取子线程结束状态和回收资源
            - pthread_join()会一直 block 父线程,直到子线程真正结束
            **分离状态**
            - 子线程执行结束后资源会被自动回收
            - 无需父线程调用pthread_join()
            - 父线程和子线程相互独立,子线程结束不会影响父线程
            所以,我们会根据实际需要选择让线程处于可连接状态还是分离状态:
            - 需要获取线程结束状态或回收资源时,使用可连接状态,需要调用pthread_join()
            - 线程完全自主执行,结束后资源自动回收,使用分离状态,调用pthread_detach()
         * */
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}
template <typename T>
bool threadpool<T>::append(T *request)
{
    m_queuelocker.lock();  // 加锁实现多线程并发往队列塞请求
    if (m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();  // TODO 为什么不在锁住的时候 +1 是为了唤醒吗
    // P 操作：将 sem 减 1，相减后，如果 sem < 0，则进程/线程进入阻塞等待，
    // 否则继续，表明 P 操作可能会阻塞；
    // V 操作：将 sem 加 1，相加后，如果 sem <= 0，唤醒一个等待中的进程/线程，
    // 表明 V 操作不会阻塞；
    return true;
}
// 线程处理函数
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}
template <typename T>
void threadpool<T>::run()
{
    while (!m_stop)
    {
        m_queuestat.wait();
        // P 操作：将 sem 减 1，相减后，如果 sem < 0，则进程/线程进入阻塞等待，否则继续，
        // 表明 P 操作可能会阻塞；
        // V 操作：将 sem 加 1，相加后，如果 sem <= 0，唤醒一个等待中的进程/线程，
        // 表明 V 操作不会阻塞；
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;

        connectionRAII mysqlcon(&request->mysql, m_connPool);
        
        request->process();
    }
}
#endif
