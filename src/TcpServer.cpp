#include "TcpServer.h"
#include "EventLoop.h"

#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>

namespace {
std::uint32_t ReadUint32(const char* data) {
    std::uint32_t net_value = 0;
    std::memcpy(&net_value, data, sizeof(net_value));
    return ntohl(net_value);
}

void AppendUint32(std::string& buffer, std::uint32_t value) {
    std::uint32_t net_value = htonl(value);
    const char* bytes = reinterpret_cast<const char*>(&net_value);
    buffer.append(bytes, sizeof(net_value));
}
}

TcpServer::TcpServer(int port, bool edge_triggered)
    : port_(port), edge_triggered_(edge_triggered), event_loop_(nullptr) {}

bool TcpServer::SetNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        logger_.Error("fcntl(F_GETFL) failed: " + std::string(std::strerror(errno)));
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        logger_.Error("fcntl(F_SETFL, O_NONBLOCK) failed: " + std::string(std::strerror(errno)));
        return false;
    }

    return true;
}

bool TcpServer::Start() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        logger_.Error("Failed to create socket: " + std::string(std::strerror(errno)));
        return false;
    }
    listen_fd_.Reset(listen_fd);

    if (!SetNonBlocking(listen_fd_.Get())) {
        listen_fd_.Reset();
        return false;
    }

    int opt = 1;
    if (setsockopt(listen_fd_.Get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        logger_.Error("Failed to set socket options: " + std::string(std::strerror(errno)));
        listen_fd_.Reset();
        return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd_.Get(),
             reinterpret_cast<sockaddr*>(&server_addr),
             sizeof(server_addr)) < 0) {
        logger_.Error("Failed to bind socket: " + std::string(std::strerror(errno)));
        listen_fd_.Reset();
        return false;
    }

    if (listen(listen_fd_.Get(), 5) < 0) {
        logger_.Error("Failed to listen on socket: " + std::string(std::strerror(errno)));
        listen_fd_.Reset();
        return false;
    }

    if (!InitEventLoop()) {
        return false;
    }

    logger_.Info("Server started on port " + std::to_string(port_) +
                 " (non-blocking, trigger_mode=" + std::string(edge_triggered_ ? "ET" : "LT") + ")");
    return true;
}

bool TcpServer::SendFrame(Connection& connection, const MessageFrame& frame) {
    if (frame.body.size() > kMaxBodyLen) {
        logger_.Error("refuse to send oversized body to " + connection.peer +
                      ", request_id=" + std::to_string(frame.request_id));
        return false;
    }

    AppendUint32(connection.output_buffer, frame.cmd);
    AppendUint32(connection.output_buffer, frame.request_id);
    AppendUint32(connection.output_buffer, static_cast<std::uint32_t>(frame.body.size()));
    connection.output_buffer.append(frame.body);
    return true;
}

TcpServer::ParseResult TcpServer::TryParseFrame(std::string& input_buffer, MessageFrame& frame) {
    if (input_buffer.size() < kHeaderLen) {
        return ParseResult::kIncomplete;
    }

    const char* data = input_buffer.data();
    std::uint32_t cmd = ReadUint32(data);
    std::uint32_t request_id = ReadUint32(data + 4);
    std::uint32_t body_len = ReadUint32(data + 8);

    if (body_len > kMaxBodyLen) {
        logger_.Error("invalid frame: body too large, cmd=" + std::to_string(cmd) +
                      ", request_id=" + std::to_string(request_id) +
                      ", body_len=" + std::to_string(body_len));
        return ParseResult::kInvalid;
    }

    if (input_buffer.size() < kHeaderLen + body_len) {
        return ParseResult::kIncomplete;
    }

    frame.cmd = cmd;
    frame.request_id = request_id;
    frame.body.assign(input_buffer.data() + kHeaderLen, body_len);
    input_buffer.erase(0, kHeaderLen + body_len);
    return ParseResult::kComplete;
}

TcpServer::MessageFrame TcpServer::ProcessMessage(const Connection& connection,
                                                  const MessageFrame& request) {
    MessageFrame response;
    response.cmd = request.cmd;
    response.request_id = request.request_id;
    response.body = "server(" + connection.peer + ") cmd=" + std::to_string(request.cmd) +
                    " request_id=" + std::to_string(request.request_id) +
                    " body=" + request.body;
    return response;
}

bool TcpServer::HandleRead(int conn_fd, Connection& connection) {
    char temp[1024];

    while (true) {
        ssize_t n = recv(conn_fd, temp, sizeof(temp), 0);
        if (n > 0) {
            connection.input_buffer.append(temp, static_cast<std::size_t>(n));

            while (true) {
                MessageFrame request;
                ParseResult result = TryParseFrame(connection.input_buffer, request);
                if (result == ParseResult::kIncomplete) {
                    break;
                }
                if (result == ParseResult::kInvalid) {
                    logger_.Error("close connection due to invalid frame: " + connection.peer);
                    return false;
                }

                logger_.Info("frame from " + connection.peer +
                             ", cmd=" + std::to_string(request.cmd) +
                             ", request_id=" + std::to_string(request.request_id) +
                             ", body=" + request.body);

                MessageFrame response = ProcessMessage(connection, request);
                if (!SendFrame(connection, response)) {
                    return false;
                }
            }
            continue;
        }

        if (n == 0) {
            logger_.Info("client disconnected: " + connection.peer);
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }

        logger_.Error("recv() failed for " + connection.peer + ": " + std::string(std::strerror(errno)));
        return false;
    }
}

bool TcpServer::HandleWrite(int conn_fd, Connection& connection) {
    while (!connection.output_buffer.empty()) {
        ssize_t n = send(conn_fd,
                         connection.output_buffer.data(),
                         connection.output_buffer.size(),
                         MSG_NOSIGNAL);
        if (n > 0) {
            connection.output_buffer.erase(0, static_cast<std::size_t>(n));
            continue;
        }

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }

        logger_.Error("send() failed for " + connection.peer + ": " + std::string(std::strerror(errno)));
        return false;
    }

    return true;
}

void TcpServer::CloseConnection(int conn_fd) {
    auto it = connections_.find(conn_fd);
    if (it != connections_.end()) {
        if (event_loop_ != nullptr) {
            event_loop_->Remove(conn_fd);
        }
        close(conn_fd);
        connections_.erase(it);
        return;
    }

    close(conn_fd);
}

bool TcpServer::InitEventLoop() {
    delete event_loop_;
    event_loop_ = new EventLoop(logger_);

    if (!event_loop_->Init()) {
        delete event_loop_;
        event_loop_ = nullptr;
        return false;
    }

    if (!event_loop_->Add(listen_fd_.Get(), BuildEpollEvents(EPOLLIN))) {
        delete event_loop_;
        event_loop_ = nullptr;
        return false;
    }

    logger_.Info("EventLoop initialized: listen fd registered for EPOLLIN" +
                 std::string(edge_triggered_ ? "|EPOLLET" : " (LT)"));
    return true;
}

bool TcpServer::UpdateConnectionEvents(int conn_fd, const Connection& connection) {
    std::uint32_t events = EPOLLIN | EPOLLRDHUP;
    if (!connection.output_buffer.empty()) {
        events |= EPOLLOUT;
    }

    return event_loop_->Modify(conn_fd, BuildEpollEvents(events));
}

std::uint32_t TcpServer::BuildEpollEvents(std::uint32_t base_events) const {
    if (edge_triggered_) {
        return base_events | EPOLLET;
    }
    return base_events;
}

void TcpServer::AcceptNewConnections() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int conn_fd = accept(listen_fd_.Get(),
                             reinterpret_cast<sockaddr*>(&client_addr),
                             &client_len);
        if (conn_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            logger_.Error("accept() failed: " + std::string(std::strerror(errno)));
            break;
        }

        if (!SetNonBlocking(conn_fd)) {
            close(conn_fd);
            continue;
        }

        char client_ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);

        Connection connection;
        connection.peer = std::string(client_ip) + ":" + std::to_string(client_port);
        auto [it, inserted] = connections_.emplace(conn_fd, std::move(connection));
        if (!inserted) {
            logger_.Error("duplicate connection fd detected: fd=" + std::to_string(conn_fd));
            close(conn_fd);
            continue;
        }

        if (!event_loop_->Add(conn_fd, BuildEpollEvents(EPOLLIN | EPOLLRDHUP))) {
            logger_.Error("failed to register conn fd into epoll: fd=" + std::to_string(conn_fd));
            close(conn_fd);
            connections_.erase(it);
            continue;
        }

        logger_.Info("new client connected: fd=" + std::to_string(conn_fd) +
                     ", peer=" + it->second.peer + ", registered to epoll");
    }
}

void TcpServer::Run() {
    if (!listen_fd_.IsValid()) {
        logger_.Error("Run() failed: listen_fd_ is invalid");
        return;
    }
    if (event_loop_ == nullptr) {
        logger_.Error("Run() failed: event_loop_ is not initialized");
        return;
    }

    logger_.Info("Server is running, waiting for connections...");

    while (true) {
        std::vector<EventLoop::ReadyEvent> ready_events = event_loop_->Wait(1000);
        for (const auto& event : ready_events) {
            if (event.fd == listen_fd_.Get()) {
                logger_.Info("epoll event: listen fd is readable");
                AcceptNewConnections();
                continue;
            }

            auto it = connections_.find(event.fd);
            if (it == connections_.end()) {
                continue;
            }

            int conn_fd = it->first;
            Connection& connection = it->second;

            if (event.error || event.hangup) {
                logger_.Info("closing connection due to epoll error/hangup: fd=" +
                             std::to_string(conn_fd) + ", peer=" + connection.peer);
                CloseConnection(conn_fd);
                continue;
            }

            bool keep_alive = true;
            if (event.readable) {
                keep_alive = HandleRead(conn_fd, connection);
            }
            if (keep_alive && (event.writable || !connection.output_buffer.empty())) {
                keep_alive = HandleWrite(conn_fd, connection);
            }

            if (!keep_alive) {
                logger_.Info("closing connection: fd=" + std::to_string(conn_fd) +
                             ", peer=" + connection.peer);
                CloseConnection(conn_fd);
                continue;
            }

            if (!UpdateConnectionEvents(conn_fd, connection)) {
                logger_.Info("closing connection due to epoll update failure: fd=" +
                             std::to_string(conn_fd) + ", peer=" + connection.peer);
                CloseConnection(conn_fd);
            }
        }
    }
}

void TcpServer::Stop() {
    for (auto it = connections_.begin(); it != connections_.end();) {
        int conn_fd = it->first;
        ++it;
        close(conn_fd);
    }
    connections_.clear();

    if (event_loop_ != nullptr) {
        delete event_loop_;
        event_loop_ = nullptr;
    }

    if (listen_fd_.IsValid()) {
        listen_fd_.Reset();
        logger_.Info("server stopped");
    }
}
