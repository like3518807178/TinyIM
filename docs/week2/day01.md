# 2026-03-23（周一）

## 今日目标
- 引入 `EventLoop` 骨架
- 创建 `epoll` 实例
- 注册监听 fd 的可读事件
- 先让“新连接接入”走 `epoll`
- 顺手补一层 C++ RAII 基础，为后续 socket / DB 资源包装做准备

## 今日先遍历了哪些文件
- `CMakeLists.txt`
- `README.md`
- `include/TcpServer.h`
- `src/TcpServer.cpp`
- `src/main.cpp`
- `docs/week1/day02.md`
- `docs/week1/day05.md`
- `docs/week1/day06.md`
- `docs/week1/decision_log.md`

这次遍历的重点不是把所有历史文档逐行重读，而是先确认两件事：
- 当前服务端的连接接入路径在哪里
- Week 1 已经把哪些非阻塞语义打好了底

结论：
- 当前接入路径在 `TcpServer::Run()`，原来是手写轮询 `accept()`
- 监听 fd 和连接 fd 已经都是非阻塞
- 现有连接的读写、拆包、写缓冲逻辑已经能继续复用
- 所以今天可以只把“新连接接入”切到 `epoll`，不需要把整个 I/O 模型一次性推翻

## 今日实现

### 1. 新增 `ScopedFd`，先把 fd 生命周期收口
新增文件：
- `include/ScopedFd.h`

做的事情：
- 封装 fd 的关闭逻辑
- 禁止拷贝，允许移动
- 在析构时自动 `close()`
- 提供 `Get()` / `Reset()` / `Release()` 基础接口

这一步的价值：
- 先把“谁负责关 fd”这个问题说清楚
- 为后面包装 socket、数据库连接等资源做 RAII 练习

虽然今天只先用在监听 fd 和 `epoll` fd 上，但方向已经对了。

### 2. 新增 `EventLoop` 骨架
新增文件：
- `include/EventLoop.h`
- `src/EventLoop.cpp`

当前 `EventLoop` 只做最小能力：
- `epoll_create1()` 创建 `epoll` 实例
- `epoll_ctl(..., EPOLL_CTL_ADD, ...)` 注册监听 fd 的 `EPOLLIN`
- `epoll_wait()` 等待事件并返回 ready fd 列表

刻意没有一次做太多：
- 还没有把连接 fd 也注册进 `epoll`
- 还没有读写事件分发
- 还没有抽成完整 Reactor

今天就是先搭骨架，保证接入点切换成功。

### 3. `TcpServer` 改成“epoll 触发 accept”
改动文件：
- `include/TcpServer.h`
- `src/TcpServer.cpp`
- `CMakeLists.txt`

核心调整：
- `listen_fd_` 从裸 `int` 改成 `ScopedFd`
- `Start()` 成功监听后，初始化 `EventLoop`
- 把监听 fd 注册到 `epoll`
- 新增 `AcceptNewConnections()`，专门处理一轮批量 `accept()`
- `Run()` 中不再直接先手写轮询监听 fd，而是先调用 `event_loop_->Wait(1)`
- 当 ready fd 命中监听 fd 时，再进入 `AcceptNewConnections()`

这样一来，新连接的建立路径就变成了：

```text
客户端发起连接
-> 监听 fd 在 epoll 中变为可读
-> epoll_wait() 返回事件
-> TcpServer::AcceptNewConnections()
-> accept() 拿到 conn_fd
```

### 4. 现阶段保留的实现
为了只做“骨架”，今天没有把全部连接收发也迁到 `epoll`：
- 现有连接的 `HandleRead()` / `HandleWrite()` 仍沿用原先逻辑
- `Run()` 每轮在处理完 `epoll` 事件后，仍会遍历 `connections_`

这样做的好处是：
- 改动范围小
- 验收目标聚焦
- 不会因为一次性改太多，打乱 Week 1 已经稳定下来的读写语义

## 本地验收
本地执行：

```bash
cmake -S /home/like/TinyIM -B /home/like/TinyIM/build
cmake --build /home/like/TinyIM/build
cd /home/like/TinyIM/build
./TinyIM
```

再用一个本地短连接打到 `127.0.0.1:9999`。

服务端日志关键片段如下：

```text
[INFO] EventLoop initialized: listen fd registered for EPOLLIN
[INFO] Server started on port 9999 (non-blocking)
[INFO] Server is running, waiting for connections...
[INFO] epoll event: listen fd is readable
[INFO] new client connected: fd=5, peer=127.0.0.1:42440
```

说明：
- `epoll` 已经创建成功
- 监听 fd 已经注册成功
- 新连接不是靠原来的直接轮询撞上的，而是由 `epoll` 事件触发进入 `accept()`
- 服务端还能继续正常运行

验收结果：通过。

## 今天对 RAII 的理解
今天这个练习里，RAII 最重要的不是“写一个类”本身，而是建立这种意识：

- 资源获取后，必须立刻明确释放责任
- 资源对象应该跟作用域绑定，而不是靠人脑记得 `close()`
- 只要后面还会管理 socket、`epoll` fd、数据库连接、文件句柄，RAII 都值得尽早建立

这也是为什么今天虽然只做 `EventLoop` 骨架，还是顺手把 `ScopedFd` 放进来了。

## 当前限制
- 现在只有监听 fd 进入了 `epoll`
- 已连接 fd 还没有注册读写事件
- 主循环里仍保留 `connections_` 遍历
- `EventLoop` 现在只是最小封装，还不是完整 Reactor

## 明日可接续方向
- 把连接 fd 的可读事件也注册到 `epoll`
- 去掉对所有连接的手写遍历
- 按事件分发 `HandleRead()` / `HandleWrite()`
- 继续把裸资源替换成 RAII 包装
