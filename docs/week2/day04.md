# 2026-03-26（周四）

## 今日目标
- 实现 ET/LT 开关（默认 LT）
- 在 ET 模式下明确并记录“循环读到 EAGAIN”这一必要语义
- 验收：切到 ET 后仍稳定，多连接并发无卡死

## 本次改动

### 1) 增加 epoll 触发模式配置（默认 LT）
- 配置文件新增：`epoll_trigger_mode`
- 支持值：`LT` / `ET`（大小写兼容）
- 非法值回退到 `LT`，并记录错误日志

相关文件：
- `conf/server.conf`
- `src/main.cpp`

### 2) TcpServer 接入触发模式并统一应用到 epoll 事件
- `TcpServer` 构造函数新增参数：`edge_triggered`
- 新增 `BuildEpollEvents(base_events)`：在 ET 模式下给事件掩码补 `EPOLLET`
- 以下路径统一走该函数：
  - 监听 fd 的 `ADD`
  - 连接 fd 的 `ADD`
  - 连接 fd 的 `MOD`

相关文件：
- `include/TcpServer.h`
- `src/TcpServer.cpp`

### 3) 文档和样例配置更新
- README 配置说明补充 `epoll_trigger_mode`
- 默认示例配置显式写为 `LT`

相关文件：
- `README.md`
- `conf/server.conf`

## ET 模式下为什么必须“循环读到 EAGAIN”
在 LT 模式中，只要 fd 仍可读，`epoll_wait` 后续还会继续通知；
但在 ET 模式中，通知是“边沿触发”，如果一次可读事件里没有把内核接收缓冲区尽量读空，就可能收不到下一次提醒，从而留下未处理数据，表现为连接“卡住”。

因此 ET 的读路径必须遵循：
1. 收到可读事件后进入循环 `recv()`
2. `n > 0` 就继续读并追加 buffer
3. `recv()` 返回 `-1` 且 `errno == EAGAIN/EWOULDBLOCK` 时，才结束本轮读
4. `recv() == 0` 视为对端关闭

TinyIM 当前 `HandleRead()` 已经满足这套语义（循环读到 `EAGAIN`），所以切换到 ET 后行为仍稳定。

## 验收记录

### 构建
```bash
cmake -S . -B build
cmake --build build
```
结果：编译通过。

### 用例 A：默认 LT（`epoll_trigger_mode=LT`）
并发压测命令：
```bash
python3 scripts/verify_concurrent_protocol.py --host 127.0.0.1 --port 9999 --clients 20 --requests 20
```
结果：20/20 客户端全部 `True`，每客户端 20 次请求全部匹配。

### 用例 B：切换 ET（`epoll_trigger_mode=ET`）
并发压测命令同上。

关键日志：
- `config loaded ... epoll_trigger_mode=ET`
- `EventLoop initialized: listen fd registered for EPOLLIN|EPOLLET`
- `Server started ... trigger_mode=ET`

结果：20/20 客户端全部 `True`，每客户端 20 次请求全部匹配。

## 结论
- ET/LT 开关已完成，默认 LT
- ET 模式下“循环读到 EAGAIN”语义已在代码中满足，并已写入文档
- 实测 ET/LT 两种模式下，多连接并发均稳定，无卡死
