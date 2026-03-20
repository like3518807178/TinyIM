# TinyIM

TinyIM 是一个基于 Linux + C++17 的即时通讯服务端练习项目。

当前阶段已经完成的内容包括：
- 最小 TCP 服务端拆分为 `TcpServer` 和 `Logger`
- 服务端循环 `accept()`，客户端断开后服务端不会退出
- 日志输出带时间戳
- 支持通过配置文件读取端口和日志级别
- CMake 使用 target 级 include 管理头文件目录

## 目录结构

```text
TinyIM/
├── CMakeLists.txt
├── README.md
├── conf/
│   └── server.conf
├── docs/
│   ├── day01.md
│   ├── day02.md
│   └── day03.md
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

## 构建方式

在项目根目录执行：

```bash
cmake -S . -B build
cmake --build build
```

构建完成后，可执行文件位于：

```text
build/TinyIM
```

## 运行方式

需要从 `build/` 目录启动程序：

```bash
cd build
./TinyIM
```

原因是当前 `main.cpp` 中读取配置文件的路径为：

```cpp
../conf/server.conf
```

所以如果不在 `build/` 目录下运行，程序可能会找不到配置文件。

## 配置文件

配置文件位置：

```text
conf/server.conf
```

当前示例：

```conf
port=9999
log_level=INFO
```

支持的配置项：
- `port`：服务端监听端口
- `log_level`：日志级别，当前代码中常用值为 `INFO` / `ERROR`

## 修改端口后启动

如果想改端口，可以直接编辑：

```text
conf/server.conf
```

例如改成：

```conf
port=10001
log_level=INFO
```

然后重新启动服务端：

```bash
cd build
./TinyIM
```

启动成功后，日志中会看到类似输出：

```text
[2026-03-20 19:31:24] [INFO] config loaded, port=10001, log_level=INFO
[2026-03-20 19:31:24] [INFO] Server started on port 10001
```

这说明配置文件中的端口已经生效。

## 验收情况

当前已经验证通过的项目行为：
- 客户端断开后，服务端不会退出
- 服务端可以持续监听端口并接受新的连接
- 连续三次重连后，仍然可以正常收发消息
- 修改配置文件中的端口后，可以按新端口成功启动

## 调试命令

查看监听端口：

```bash
ss -ltnp | grep 9999
```

查看端口被哪个进程占用：

```bash
lsof -i :9999
```

查看服务端进程：

```bash
ps -ef | grep TinyIM
```

动态查看系统状态：

```bash
top
```

## 开发记录

- Day 01: [`docs/day01.md`](docs/day01.md)
- Day 02: [`docs/day02.md`](docs/day02.md)
- Day 03: [`docs/day03.md`](docs/day03.md)
