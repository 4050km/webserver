#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>
//ʹ�����ֻ���ʵ���߳�ͬ����POSIX�ź���������������������

class sem//�ź�����Semaphore�����ʵ�֡����ƶԹ�����Դ�Ķ�ռ����
{
//����ڻ��������ź����������ڸ����ӵ�ͬ����ͨ�ų������ź������������ڿ��ƶ�һ����Դ�ķ��ʣ�ʵ���̼߳��ͬ����Э����
public:
    sem()//Ĭ�Ϲ��캯��������һ����ʼֵΪ0���ź�����
    {
        //��һ������ָ�����������ź������ڶ�������ָ���ź������ͣ������ֵΪ0��ô��ʾ����ź����ǵ�ǰ���̵ľֲ��ź���������������ָ���źų�ʼֵ
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();//��ʼ����Ϊ����ʧ�ܣ����׳��쳣
        }
    }
    sem(int num)//�������Ĺ��캯��������һ����ʼֵΪ num ���ź�����
    {
        if (sem_init(&m_sem, 0, num) != 0)//����ָ���ź�����ʼֵ
        {
            throw std::exception();
        }
    }
    ~sem()//�������������������ź�����
    {
        sem_destroy(&m_sem);
    }
    bool wait()//��ԭ�Ӳ������ź�����ֵ��1�����Ϊ����������
    {
        return sem_wait(&m_sem) == 0;
    }
    bool post()
    {
        //��ԭ�Ӳ������ź���+1�����ź�����ֵ������ʱ���������ڵ���sem_wait�ȴ��ź������߳̽�������
        return sem_post(&m_sem) == 0;
    }

private:
    //����һ���ź�������m_sem
    sem_t m_sem;
};
class locker//����������ͬ���̶߳Թ�����Դ�ķ���
    // ��������Ҫ�����ṩ�Թ�����Դ�Ļ�����ʣ�ȷ��������ʱ��ֻ��һ���߳̿��Է��ʹ�����Դ���Ӷ����⾺̬���������ݲ�һ���ԡ�
{
//��һ���̻߳���˻������������������������߳����Ҳ��ͼ��ȡ�������ǻᱻ������ֱ�����������߳��ͷ�����
//�����ͱ�֤�����ٽ����ڵĴ��룬ֻ��һ���߳̿���ִ�У��Ӷ������˶��̻߳����¶Թ�����Դ�ľ���������
public:
    locker()//��ʼ����
    {
        //��δ���ʹ�� pthread_mutex_init ������ʼ���� m_mutex������������
        // pthread_mutex_init �᷵��0��ʾ�ɹ���
      
        //��һ��������ʾҪ�����Ļ��������ڶ�������ָ�������������ԣ�null��ʾĬ������
        if (pthread_mutex_init(&m_mutex, NULL) != 0)//
        {
            throw std::exception();
        }
    }
    ~locker()//���������ͷ���ռ�õ��ں���Դ
    {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock()//��ʵ���ǵ��� pthread_mutex_lock ������
    {
        //��ԭ�Ӳ����ķ�ʽ��һ�����������������Ŀ�껥�����Ѿ����ϣ���pthread_mutex_lock������
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock()//����
    {
        //��ԭ�Ӳ����ķ�ʽ��һ�������������������ʱ�������߳����ڵȴ����������������Щ�߳��е�ĳһ������������
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    // ������ָ�� pthread_mutex_t ���͵ĳ�Ա���� m_mutex ��ָ��
    // ��������������ⲿ�����ȡ locker ����ʹ�õĻ��������Ա�����Ҫʱ���ж���Ĳ�������������ʲô�����أ� ��־��ȡ������
    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    //����һ������������ m_mutex
    pthread_mutex_t m_mutex;//m_mutex ������ʵ�ֶ��ٽ����ļ����ͽ�����
};
class cond 
    //����������ͬ���̶߳Թ������ݵķ��ʣ����������������߳�֮��ͬ���������ݵ�ֵ

    //ͨ���ͻ�����һ��ʹ�ã���ʵ���̵߳ĵȴ��ͻ��ѻ��ơ�
    //���������ṩ��һ���̼߳��֪ͨ����,��ĳ���������ݴﵽĳ��ֵʱ,���ѵȴ�����������ݵ��߳�.
{
public:
    cond()//��ʼ����������
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)//���캯������ʼ�����������������ʼ��ʧ�ܣ��׳��쳣��
        {
            //pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~cond()//������������
    {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t *m_mutex)//�ȴ�Ŀ��������������
    {
        int ret = 0;
        //�ڵ���pthread_cond_waitǰ����ȷ��m_mutex�Ѿ�����������ᵼ�²���Ԥ�ڵĽ��
        //pthread_mutex_lock(&m_mutex);//�����п���ҲӦ��ע�ͣ���Ϊ����Ĵ�����ܻ��������Ŀǰ��û�ҵ���Ӧ������λ��
        //���ڵȴ�Ŀ�������������ڶ�������ʱ���ڱ������������Ļ�������ȷ��pthread_cond_wait������ԭ���ԣ���ʵ��Ϊ�˱�֤phtread_cond_wait��ʼִ�е�������̱߳��������������ĵȴ��������ʱ����
        // �������������޸�����������
        ret = pthread_cond_wait(&m_cond, m_mutex);//��ȴ���������m_cond���㣬ͬʱ�Ὣ������m_mutex���ݸ��ú����������ڵȴ��ڼ��ͷŻ����������������̷߳��ʹ�����Դ��
        //pthread_mutex_unlock(&m_mutex);����ĺ������Զ��ͷŻ����������д��������
        return ret == 0;//��ʾ���pthread_cond_wait��������0�����ȴ��ɹ�����ô��������true�����򷵻�false��
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)//�ȴ�Ŀ�������������Ǵ��г�ʱ�����ĺ���
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);//��pthread_cond_wait�������ǣ�����һ����ʱ����&t����ʾ��ȴ���ʱ�䡣����ڳ�ʱǰ�������㣬���߳�ʱʱ������δ���㣬���������ء�
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool signal()//���ڻ���һ���ȴ�Ŀ�������������̵߳ĺ���
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast()//�Թ㲥�ķ�ʽ�������еȴ�Ŀ�������������̵߳ĺ���
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    //static pthread_mutex_t m_mutex;
    //������������m_cond
    pthread_cond_t m_cond;
};
#endif
