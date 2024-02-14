#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	this->CurConn = 0;
	this->FreeConn = 0;
}

connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;//使用 static 修饰的局部变量在函数首次被调用时初始化，并在程序的生命周期内保持存在。
	//由于这个对象是在函数内部声明的，因此它的生命周期限定在这个函数内，而通过返回指针，可以在其他地方获取到这个单例对象。
	//这样设计的目的是确保系统中只有一个 connection_pool 实例，这在数据库连接池等需要全局唯一性的情况下是很常见的做法。
	//这可以防止创建多个连接池，确保全局共享一个连接池实例。
	return &connPool;
}

//构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, unsigned int MaxConn)
{   // 初始化连接池的各个属性
	this->url = url;
	this->Port = Port;
	this->User = User;
	this->PassWord = PassWord;
	this->DatabaseName = DBName;
	// 加锁，防止多线程同时创建连接池
	lock.lock();
	// 循环创建连接对象，并加入连接池列表，MaxConn是连接池中最大数据库连接的数量
	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = NULL;//初始化一个mysql连接对象con为null
		con = mysql_init(con);//初始化mysql对象，并将指针 con 指向这个对象。

		if (con == NULL)//这段代码检查 mysql_init 的返回值，如果返回的 MYSQL 对象为空（即初始化失败），
		//则输出错误信息和 MySQL 提供的错误描述，并终止程序执行。
		{
			cout << "Error:" << mysql_error(con);
			exit(1);//表示此进程结束
		}
		//mysql_real_connect 函数来建立一个实际的 MySQL 连接。
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);
		//失败则显示错误信息
		if (con == NULL)
		{
			cout << "Error: " << mysql_error(con);
			exit(1);
		}
		connList.push_back(con);//如果成功则加入头文件中定义的mysql连接池
		++FreeConn;
	}
	//调用sem(int num)//带参数的构造函数，创建一个初始值为 num 的信号量，表示当前可用连接数
	reserve = sem(FreeConn);//当前可用连接数

	// 最大连接数为可用连接数
	this->MaxConn = FreeConn;
	//解锁
	lock.unlock();
}


//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;//初始化mysql连接对象con

	if (0 == connList.size())//如果连接池为零那么返回空指针
		return NULL;

	reserve.wait();//以原子操作减一，为零阻塞
	
	lock.lock();//加锁

	con = connList.front();//con = connList.front()获取连接池中的第一个连接，
	connList.pop_front();//然后将其从连接池中移除。这表示这个连接被分配给了调用该函数的线程。

	--FreeConn;//连接池可用连接减一
	++CurConn;//当前连接加1

	lock.unlock();//解锁
	return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();//加锁

	connList.push_back(con);//将连接放回连接池
	++FreeConn;
	--CurConn;

	lock.unlock();//解锁

	reserve.post();//发布信号量，将信号加1，表示可用连接加1
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{

	lock.lock();//加锁
	if (connList.size() > 0)
	{
		//通过迭代器遍历，关闭数据库连接

		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)//遍历连接池
		{
			MYSQL *con = *it;
			mysql_close(con);//关闭连接
		}
		CurConn = 0;//都置零和清空
		FreeConn = 0;
		connList.clear();

		lock.unlock();
	}

	lock.unlock();//解锁
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();//连接池销毁
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	// 从连接池获取数据库连接，将连接指针保存在 SQL（其实就是mysqlcon(&mysql, connPool)中的第一个参数mysql指向的地址中）
	//这里的*SQL实际上等于request->mysql，也就是一个数据库连接对象，但这个数据库连接对象来自于http_conn类,所以一个http请求获取一个数据库连接池中的一个数据库连接对象
	*SQL = connPool->GetConnection();
	// 将连接指针和连接池指针保存在成员变量中，用来最后进行最后对象的释放，RAII这也就是RAII的作用，使得资源和对象的声明周期绑定在一起
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	// 通过RAII释放对应对象所占有的数据库连接池中的一条数据库连接资源，从而实现对象和资源的声明周期相同
	poolRAII->ReleaseConnection(conRAII);
}