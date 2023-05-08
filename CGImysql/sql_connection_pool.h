#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"

using namespace std;

class connection_pool  // 连接池
{
public:
	MYSQL *GetConnection();				 //获取数据库连接
	bool ReleaseConnection(MYSQL *conn); //释放连接
	int GetFreeConn();					 //获取获取当前空闲的连接数，return this->FreeConn;
	void DestroyPool();					 //销毁所有连接

	//单例模式，创建全局唯一的连接池
    // static 提供访问该单例对象的全局访问点
	static connection_pool *GetInstance();

	void init(string url, string User, string PassWord, string DataBaseName, int Port, unsigned int MaxConn); 
	
	connection_pool();
	~connection_pool();

private:
	unsigned int MaxConn;  //最大连接数
	unsigned int CurConn;  //当前已使用的连接数
	unsigned int FreeConn; //当前空闲的连接数

private:
	locker lock;
	list<MYSQL *> connList; //连接池
	sem reserve;  // 信号量，初始化为最大连接次数

private:
	string url;			 //数据库主机地址
	string Port;		 //数据库端口号
	string User;		 //登陆数据库用户名
	string PassWord;	 //登陆数据库密码
	string DatabaseName; //使用数据库名
};

// 将数据库连接的获取与释放通过RAII机制封装，避免手动释放（用完了放回去，不是销毁）。
class connectionRAII{

public:
    // 构造方法，将数据库连接的获取封装起来，内部调用了connPool->GetConnection();
	connectionRAII(MYSQL **con, connection_pool *connPool);
    // 用完了放回去，调用了ReleaseConnection(MYSQL *con)
	~connectionRAII();
	
private:
	MYSQL *conRAII;  // 一个数据库连接指针
	connection_pool *poolRAII;  // 数据库连接池指针
};

#endif
