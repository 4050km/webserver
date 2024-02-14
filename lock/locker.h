#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>
//使用三种机制实现线程同步：POSIX信号量、互斥量、条件变量

class sem//信号量（Semaphore）类的实现。控制对共享资源的独占访问
{
//相比于互斥锁，信号量可以用于更复杂的同步和通信场景。信号量还可以用于控制对一组资源的访问，实现线程间的同步和协调。
public:
    sem()//默认构造函数，创建一个初始值为0的信号量。
    {
        //第一个参数指定被操作的信号量，第二个参数指定信号量类型，如果其值为0那么表示这个信号量是当前进程的局部信号量，第三个参数指定信号初始值
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();//初始化不为零则失败，则抛出异常
        }
    }
    sem(int num)//带参数的构造函数，创建一个初始值为 num 的信号量。
    {
        if (sem_init(&m_sem, 0, num) != 0)//可以指定信号量初始值
        {
            throw std::exception();
        }
    }
    ~sem()//析构函数，用于销毁信号量。
    {
        sem_destroy(&m_sem);
    }
    bool wait()//以原子操作将信号量的值减1，如果为零则阻塞。
    {
        return sem_wait(&m_sem) == 0;
    }
    bool post()
    {
        //以原子操作将信号量+1，当信号量的值大于零时，其他正在调用sem_wait等待信号量的线程将被唤醒
        return sem_post(&m_sem) == 0;
    }

private:
    //声明一个信号量对象m_sem
    sem_t m_sem;
};
class locker//互斥锁用于同步线程对共享资源的访问
    // 互斥锁主要用于提供对共享资源的互斥访问，确保在任意时刻只有一个线程可以访问共享资源，从而避免竞态条件和数据不一致性。
{
//当一个线程获得了互斥锁的锁（加锁），其他线程如果也试图获取锁，它们会被阻塞，直到持有锁的线程释放锁。
//这样就保证了在临界区内的代码，只有一个线程可以执行，从而避免了多线程环境下对共享资源的竞争条件。
public:
    locker()//初始化锁
    {
        //这段代码使用 pthread_mutex_init 函数初始化了 m_mutex，即互斥锁。
        // pthread_mutex_init 会返回0表示成功，
      
        //第一个参数表示要操作的互斥锁，第二个参数指定互斥锁的属性，null表示默认属性
        if (pthread_mutex_init(&m_mutex, NULL) != 0)//
        {
            throw std::exception();
        }
    }
    ~locker()//销毁锁，释放所占用的内核资源
    {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock()//其实现是调用 pthread_mutex_lock 函数。
    {
        //以原子操作的方式给一个互斥锁加锁，如果目标互斥锁已经锁上，则pthread_mutex_lock将阻塞
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock()//解锁
    {
        //以原子操作的方式给一个互斥锁解锁，如果此时有其他线程正在等待这个互斥锁，则这些线程中的某一个将获得这个锁
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    // 它返回指向 pthread_mutex_t 类型的成员变量 m_mutex 的指针
    // 这样的设计允许外部代码获取 locker 类中使用的互斥锁，以便在需要时进行额外的操作。？具体做什么操作呢？ 日志获取互斥锁
    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    //声明一个互斥锁对象 m_mutex
    pthread_mutex_t m_mutex;//m_mutex 被用于实现对临界区的加锁和解锁。
};
class cond 
    //互斥锁用于同步线程对共享数据的访问，条件变量用于在线程之间同步共享数据的值

    //通常和互斥锁一起使用，以实现线程的等待和唤醒机制。
    //条件变量提供了一种线程间的通知机制,当某个共享数据达到某个值时,唤醒等待这个共享数据的线程.
{
public:
    cond()//初始化条件变量
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)//构造函数，初始化条件变量。如果初始化失败，抛出异常。
        {
            //pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~cond()//销毁条件变量
    {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t *m_mutex)//等待目标条件变量函数
    {
        int ret = 0;
        //在调用pthread_cond_wait前必须确保m_mutex已经加锁，否则会导致不可预期的结果
        //pthread_mutex_lock(&m_mutex);//这里有可能也应该注释，因为外面的代码可能会加锁，但目前还没找到对应加锁的位置
        //用于等待目标条件变量，第二个参数时用于保护条件变量的互斥锁，确保pthread_cond_wait操作的原子性（其实是为了保证phtread_cond_wait开始执行到其调用线程被放入条件变量的等待队列这段时间内
        // 其他函数不会修改条件变量）
        ret = pthread_cond_wait(&m_cond, m_mutex);//会等待条件变量m_cond满足，同时会将互斥锁m_mutex传递给该函数，用于在等待期间释放互斥锁，允许其他线程访问共享资源。
        //pthread_mutex_unlock(&m_mutex);上面的函数会自动释放互斥锁，这行代码多余了
        return ret == 0;//表示如果pthread_cond_wait函数返回0，即等待成功，那么函数返回true，否则返回false。
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)//等待目标条件变量但是带有超时参数的函数
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);//和pthread_cond_wait的区别是，它有一个超时参数&t，表示最长等待的时间。如果在超时前条件满足，或者超时时条件仍未满足，函数将返回。
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool signal()//用于唤醒一个等待目标条件变量的线程的函数
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast()//以广播的方式唤醒所有等待目标条件变量的线程的函数
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    //static pthread_mutex_t m_mutex;
    //声明条件变量m_cond
    pthread_cond_t m_cond;
};
#endif
