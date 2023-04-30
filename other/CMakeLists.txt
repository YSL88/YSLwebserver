cmake_minimum_required(VERSION 3.5.1)
project(ysl-TinyWebServer-raw_version)

# 设置编译选项
set(CMAKE_CXX_STANDARD 11)

# 添加源文件
add_executable(server
        main.cpp
        ./threadpool/threadpool.h
        ./http/http_conn.cpp
        ./http/http_conn.h
        ./lock/locker.h
        ./log/log.cpp
        ./log/log.h
        ./CGImysql/sql_connection_pool.cpp
        ./CGImysql/sql_connection_pool.h
        )

# 添加库链接目录和库
target_link_libraries(server
        pthread
        mysqlclient
        )

## 添加清理命令
#add_custom_target(clean
#        COMMAND rm -r server
#        )