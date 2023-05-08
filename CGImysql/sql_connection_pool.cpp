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

//实现了线程安全的懒汉单例模式。其工作原理是:
//- 第一次调用GetInstance()时,会生成静态局部对象 connPool。
//- 由于static的作用,connPool 会在程序运行期间一直存在。
//- 随后对GetInstance()的调用直接返回这个connPool对象的地址。
connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, unsigned int MaxConn)
{
	this->url = url;
	this->Port = Port;
	this->User = User;
	this->PassWord = PassWord;
	this->DatabaseName = DBName;

	lock.lock();
	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = NULL;  // con 是一个MYSQL结构体，初始化为NULL，MYSQL是MySQL C API中代表MySQL连接(Connection)的结构体类型
		con = mysql_init(con);  // 调用mysql_init()为con分配一个MYSQL结构体,并进行初始化。此时con指向一个有效的MYSQL结构体。

        if (con == NULL)
		{
			cout << "Error:" << mysql_error(con);
			exit(1);
		}
        // 使用con指向的结构体创建一个MySQL连接。如果连接成功,con仍指向原MYSQL结构体;如果失败,con会被置为NULL。
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			cout << "Error: " << mysql_error(con);
			exit(1);
		}
		connList.push_back(con);
		++FreeConn;
	}

	reserve = sem(FreeConn);  // 将信号量初始化为最大连接次数

	this->MaxConn = FreeConn;
	
	lock.unlock();
}


//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if (0 == connList.size())
		return NULL;

	reserve.wait();  // 以原子操作的方式将信号量的值减1。
	
	lock.lock();

	con = connList.front();
	connList.pop_front();

	--FreeConn;
	++CurConn;

	lock.unlock();
	return con;
}

//释放当前使用的连接，用完了放回去
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();

	connList.push_back(con);
	++FreeConn;
	--CurConn;

	lock.unlock();

	reserve.post();
	return true;
}

//销毁数据库连接池
// 通过迭代器遍历连接池链表，关闭对应数据库连接，清空链表并重置空闲连接和现有连接数量。
void connection_pool::DestroyPool()
{

	lock.lock();
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		CurConn = 0;
		FreeConn = 0;
		connList.clear();

		lock.unlock();
	}

	lock.unlock();
}

//获取当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

// 构造方法，将数据库连接的获取封装起来，内部调用了connPool->GetConnection();
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	*SQL = connPool->GetConnection();  // *SQL 才是一个 MYSQL 指针，**SQL 是指向这个指针的指针
    // 修改了 形参传过来的 SQL 指向
	
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}