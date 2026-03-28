# TinyIM

TinyIM 是一个基于 Linux + C++17 的即时通讯服务端练习项目，当前重点在于把一个最小可运行的 TCP server 逐步演进成“结构清楚、配置明确、协议可扩展、I/O 语义正确”的小型服务端。

当前代码已经覆盖：
- 配置文件加载：从 `conf/server.conf` 读取端口、日志级别和 `epoll` 触发模式
- 日志模块：统一输出时间戳与级别
- 基础消息协议：使用 `cmd + request_id + body_len + body` 切分消息帧
- 非阻塞 socket：监听 fd 和连接 fd 都设置为非阻塞
- 连接状态管理：为每个连接维护输入/输出缓冲区
- `epoll` 事件驱动：监听 fd 和连接 fd 都由 `epoll` 分发读写事件

## 仓库结构

```text
TinyIM/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── conf/
│   └── server.conf
├── docs/
│   ├── day01.md ~ day06.md
│   └── decision_log.md
├── include/
│   ├── Config.h
│   ├── Logger.h
│   └── TcpServer.h
└── src/
    ├── Config.cpp
    ├── Logger.cpp
    ├── TcpServer.cpp
    └── main.cpp
```

说明：
- `build/` 是本地构建目录，不提交到仓库
- 历史误拼目录 `buid/` 已清理，不再使用
- `docs/` 里按天记录开发过程，`decision_log.md` 单独记录关键设计选择

## 环境要求

- Linux
- CMake 3.10 及以上
- 支持 C++17 的编译器，例如 `g++`

## 怎么编译

在项目根目录执行：

```bash
cmake -S . -B build
cmake --build build
```

编译成功后，可执行文件位于：

```text
build/TinyIM
```

## 怎么运行

下面两种方式都可以：

```bash
./build/TinyIM
```

或

```bash
cd build
./TinyIM
```

程序会依次尝试读取：

```text
conf/server.conf
../conf/server.conf
```

所以无论从项目根目录还是 `build/` 目录启动，都能找到配置文件。

## 配置文件

配置文件路径：

```text
conf/server.conf
```

当前示例：

```conf
port=9999
log_level=INFO
epoll_trigger_mode=LT
```

支持的配置项：
- `port`：服务端监听端口
- `log_level`：日志级别，当前支持 `INFO` / `ERROR`
- `epoll_trigger_mode`：`epoll` 触发模式，支持 `LT` / `ET`，默认 `LT`

## 运行后的预期输出

启动成功后，日志会类似这样：

```text
[2026-03-22 10:00:00] [INFO] config loaded from conf/server.conf, port=9999, log_level=INFO, epoll_trigger_mode=LT
[2026-03-22 10:00:00] [INFO] Server started on port 9999 (non-blocking, trigger_mode=LT)
[2026-03-22 10:00:00] [INFO] Server is running, waiting for connections...
```

如果想确认进程已经在监听端口，可以执行：

```bash
ss -ltnp | grep 9999
```

## 当前实现到哪一步

目前 TinyIM 还处在“网络基础能力打底”阶段，重点不是业务功能，而是先把服务端语义做对：
- 客户端断开后，服务端不会退出
- 可以连续接受多个连接
- 可以在单连接内按 `cmd/request_id/body_len/body` 协议拆出完整消息帧
- 非阻塞读写下，会正确处理 `EAGAIN/EWOULDBLOCK`
- 写缓冲区未发完时，会保留剩余数据等待下一轮发送
- 多连接并发请求时，响应会带回原始 `request_id`

## 已知限制

- 当前还只是一个最小协议练习版本，`cmd` 还没有进入正式业务分发
- `event_loop_` 仍是裸指针管理，后续还可以继续收口
- 还没有正式的自动化测试
- 现在更适合做网络编程练习，不适合作为生产服务端


## MySQL 初始化

已经提供可一键初始化的 MySQL 脚手架：
- `sql/schema.sql`：建库建表脚本
- `docker-compose.yml`：MySQL 8.4 容器
- `scripts/init_mysql.sh`：容器模式一键初始化（推荐）
- `scripts/init_mysql_local.sh`：本地 MySQL 两步初始化

容器一键初始化：

```bash
./scripts/init_mysql.sh
```

本地 MySQL 两步初始化：

```bash
# 第一步：先确保本地 MySQL 已启动
# 第二步：执行初始化
./scripts/init_mysql_local.sh
```

默认 root 密码是 `root123`，如需改动可通过环境变量覆盖：
- `MYSQL_HOST`
- `MYSQL_PORT`
- `MYSQL_USER`
- `MYSQL_PASSWORD`

## 并发测试客户端

仓库内提供了一个最小测试客户端脚本：
- `scripts/verify_concurrent_protocol.py`

它可以完成下面三件事：
- 建立多连接并发压测
- 每个连接批量发包（支持 `--burst` 控制同一连接内的在途请求数）
- 校验服务端响应里的 `cmd`、`request_id` 和 `body`

先启动服务端：

```bash
./build/TinyIM
```

再在另一个终端执行测试：

```bash
python3 scripts/verify_concurrent_protocol.py --host 127.0.0.1 --port 9999 --clients 20 --requests 20 --burst 5
```

参数说明：
- `--clients`：并发连接数
- `--requests`：每个连接发送多少条请求
- `--burst`：每个连接允许多少条请求处于“已发送、待收响应”的状态

如果全部通过，脚本会输出每个连接的校验结果，并打印类似下面的汇总：

```text
SUMMARY: passed=20/20, requests_per_client=20, burst=5
```

这条命令就是当前仓库复现“并发连接 + 发包 + 校验响应”的推荐验收方式。

## 调试命令

查看监听端口：

```bash
ss -ltnp | grep 9999
```

查看端口占用：

```bash
lsof -i :9999
```

查看服务端进程：

```bash
ps -ef | grep TinyIM
```

动态观察资源使用：

```bash
top
```

## 开发记录

- Day 01: `docs/day01.md`
- Day 02: `docs/day02.md`
- Day 03: `docs/day03.md`
- Day 04: `docs/day04.md`
- Day 05: `docs/day05.md`
- Day 06: `docs/day06.md`
- 关键决策：`docs/decision_log.md`
