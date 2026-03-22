#include "TcpServer.h"

#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

TcpServer::TcpServer(int port) : port_(port), listen_fd_(-1) {}

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
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        logger_.Error("Failed to create socket: " + std::string(std::strerror(errno)));
        return false;
    }

    if (!SetNonBlocking(listen_fd_)) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        logger_.Error("Failed to set socket options: " + std::string(std::strerror(errno)));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd_,
             reinterpret_cast<sockaddr*>(&server_addr),
             sizeof(server_addr)) < 0) {
        logger_.Error("Failed to bind socket: " + std::string(std::strerror(errno)));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 5) < 0) {
        logger_.Error("Failed to listen on socket: " + std::string(std::strerror(errno)));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    logger_.Info("Server started on port " + std::to_string(port_) + " (non-blocking)");
    return true;
}

bool TcpServer::SendFrame(Connection& connection, const std::string& payload) {
    std::uint32_t body_len = static_cast<std::uint32_t>(payload.size());
    std::uint32_t net_len = htonl(body_len);

    std::size_t old_size = connection.output_buffer.size();
    connection.output_buffer.resize(old_size + kHeaderLen + body_len);

    std::memcpy(&connection.output_buffer[old_size], &net_len, kHeaderLen);
    if (body_len > 0) {
        std::memcpy(&connection.output_buffer[old_size + kHeaderLen], payload.data(), body_len);
    }

    return true;
}

TcpServer::ParseResult TcpServer::TryParseFrame(std::string& input_buffer, std::string& payload) {
    if (input_buffer.size() < kHeaderLen) {
        return ParseResult::kIncomplete;
    }

    std::uint32_t net_len = 0;
    std::memcpy(&net_len, input_buffer.data(), kHeaderLen);
    std::uint32_t body_len = ntohl(net_len);

    if (body_len > kMaxBodyLen) {
        logger_.Error("invalid frame: body too large, body_len=" + std::to_string(body_len));
        return ParseResult::kInvalid;
    }

    if (input_buffer.size() < kHeaderLen + body_len) {
        return ParseResult::kIncomplete;
    }

    payload.assign(input_buffer.data() + kHeaderLen, body_len);
    input_buffer.erase(0, kHeaderLen + body_len);
    return ParseResult::kComplete;
}

bool TcpServer::HandleRead(int conn_fd, Connection& connection) {
    char temp[1024];

    while (true) {
        ssize_t n = recv(conn_fd, temp, sizeof(temp), 0);
        if (n > 0) {
            connection.input_buffer.append(temp, static_cast<std::size_t>(n));

            while (true) {
                std::string payload;
                ParseResult result = TryParseFrame(connection.input_buffer, payload);
                if (result == ParseResult::kIncomplete) {
                    break;
                }
                if (result == ParseResult::kInvalid) {
                    logger_.Error("close connection due to invalid frame: " + connection.peer);
                    return false;
                }

                logger_.Info("frame payload from " + connection.peer + ": " + payload);
                SendFrame(connection, "server: " + payload);
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
        close(conn_fd);
        connections_.erase(it);
        return;
    }

    close(conn_fd);
}

void TcpServer::Run() {
    if (listen_fd_ < 0) {
        logger_.Error("Run() failed: listen_fd_ is invalid");
        return;
    }

    logger_.Info("Server is running, waiting for connections...");

    while (true) {
        while (true) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);

            int conn_fd = accept(listen_fd_,
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
            connections_.emplace(conn_fd, std::move(connection));

            logger_.Info("new client connected: fd=" + std::to_string(conn_fd) +
                         ", peer=" + connections_[conn_fd].peer);
        }

        for (auto it = connections_.begin(); it != connections_.end();) {
            int conn_fd = it->first;
            Connection& connection = it->second;

            bool keep_alive = HandleRead(conn_fd, connection);
            if (keep_alive) {
                keep_alive = HandleWrite(conn_fd, connection);
            }

            if (!keep_alive) {
                logger_.Info("closing connection: fd=" + std::to_string(conn_fd) +
                             ", peer=" + connection.peer);
                ++it;
                CloseConnection(conn_fd);
                continue;
            }

            ++it;
        }

        usleep(1000);
    }
}

void TcpServer::Stop() {
    for (auto it = connections_.begin(); it != connections_.end();) {
        int conn_fd = it->first;
        ++it;
        close(conn_fd);
    }
    connections_.clear();

    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        logger_.Info("server stopped");
    }
}
