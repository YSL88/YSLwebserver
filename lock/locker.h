#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

class sem  // RAII 实现 信号量类，P V
{
public:
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();
        }
    }
    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)
        // 初始化一个信号量，0 表示 这个信号量是当前进程的局部信号量，
        // 否则该信号量就可以在多个进程之间共享。
        // num 为 该信号量的初值
        // 初始化成功返回 0
        {
            throw std::exception();
        }
    }
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    // 以原子操作的方式将信号量的值减1
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
        // wait 成功返回 0，阻塞的，能返回就说明 减1成功，返回不了就卡住了
        // sem_wait函数以原子操作的方式将信号量的值减1。
        // 如果信号量的值为0，则sem_wait将被阻塞，直到这个信号量具有非0值。
    }
    // sem_post函数以原子操作的方式将信号量的值加1。
    // 当信号量的值大于0时，其他正在调用sem_wait等待信号量的线程将被唤醒。
    bool post()
    {
        return sem_post(&m_sem) == 0;
        // 同理
        // sem_post函数以原子操作的方式将信号量的值加1。
        // 当信号量的值大于0时，其他正在调用sem_wait等待信号量的线程将被唤醒。
    }

private:
    sem_t m_sem;  // sem_t 是一个 int
};

class locker  // RAII 互斥锁，信号值为1的信号量
{
public:
    locker()
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
        // pthread_mutex_lock函数以原子操作的方式给一个互斥锁加锁。
        // 如果目标互斥锁已经被锁上，则pthread_mutex_lock调用将阻塞，直到该互斥锁的占有者将其解锁。
        // 锁上了就 return，锁不上就卡住了
    }
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
        // pthread_mutex_unlock函数以原子操作的方式给一个互斥锁解锁。
        // 如果此时有其他线程正在等待这个互斥锁，则这些线程中的某一个将获得它。
        // 解锁后顺便唤醒一个等待线程
    }
    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;  // 锁实体
};

class cond  // 条件变量
{
public:
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            //pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        // - cond:条件变量的指针,线程会等待这个条件变量
        //- mutex:互斥锁的指针,调用线程必须持有这个互斥锁，调用前该线程就要有这个锁
        // 然后释放锁，把自己加入等待队列，等待另一个线程使用pthread_cond_signal()或pthread_cond_broadcast()函数唤醒自己
        // 醒了之后，这个线程会重新获取锁，然后 pthread_cond_wait 返回 0
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    //static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};
#endif
