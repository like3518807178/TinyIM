#include "TcpServer.h"

#include <unistd.h>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

TcpServer::TcpServer(int port):port_(port),listen_fd_(-1){};

bool TcpServer::Start(){
    //1.创建socket
    listen_fd_=socket(AF_INET,SOCK_STREAM,0);
    if(listen_fd_<0){
        logger_.Error("Failed to create socket");
        return false;
    }
    //2.opt端口复用
    int opt=1;
    if(setsockopt(listen_fd_,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt))<0){
        logger_.Error("Failed to set socket options");
        return false;
    }

    //3.准备server地址,并bind
    sockaddr_in server_addr{};
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(port_);
    server_addr.sin_addr.s_addr=INADDR_ANY;

    if(bind(listen_fd_,
            reinterpret_cast<sockaddr*>(&server_addr),
            sizeof(server_addr))<0){
        logger_.Error("Failed to bind socket");
        close(listen_fd_);
        listen_fd_=-1;
        return false;
    }

    //4.listen
    if(listen(listen_fd_,5)<0){
        logger_.Error("Failed to listen on socket");
        close(listen_fd_);
        listen_fd_=-1;
        return false;
    }
    logger_.Info("Server started on port "+std::to_string(port_));
    return true;
}

bool TcpServer::SendFrame(int conn_fd,const std::string& payload){
    std::uint32_t body_len = static_cast<std::uint32_t>(payload.size());
    std::uint32_t net_len = htonl(body_len);

    std::string frame;
    frame.resize(kHeaderLen + body_len);

    std::memcpy(&frame[0], &net_len, kHeaderLen);
    if (body_len > 0) {
        std::memcpy(&frame[kHeaderLen], payload.data(), body_len);
    }

    std::size_t total_sent = 0;
    while (total_sent < frame.size()) {
        ssize_t n = send(conn_fd,
                         frame.data() + total_sent,
                         frame.size() - total_sent,
                         0);
        if (n <= 0) {
            logger_.Error("send() failed");
            return false;
        }
        total_sent += static_cast<std::size_t>(n);
    }

    return true;
}

bool TcpServer::TryParseFrame(std::string& input_buffer, std::string& payload) {
    if (input_buffer.size() < kHeaderLen) {
        return false;
    }

    std::uint32_t net_len = 0;
    std::memcpy(&net_len, input_buffer.data(), kHeaderLen);
    std::uint32_t body_len = ntohl(net_len);

    if (body_len > kMaxBodyLen) {
        logger_.Error("frame body too large");
        input_buffer.clear();
        return false;
    }

    if (input_buffer.size() < kHeaderLen + body_len) {
        return false;
    }

    payload.assign(input_buffer.data() + kHeaderLen, body_len);
    input_buffer.erase(0, kHeaderLen + body_len);
    return true;
}


void TcpServer::Run() {
    if (listen_fd_ < 0) {
        logger_.Error("Run() failed: listen_fd_ is invalid");
        return;
    }

    logger_.Info("Server is running..., waiting for connections...");

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int conn_fd = accept(listen_fd_,
                             reinterpret_cast<sockaddr*>(&client_addr),
                             &client_len);
        if (conn_fd < 0) {
            logger_.Error("Failed to accept connection");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);

        logger_.Info("new client connected: " +
                     std::string(client_ip) + ":" + std::to_string(client_port));

        std::string input_buffer;
        char temp[1024];

        while (true) {
            int n = recv(conn_fd, temp, sizeof(temp), 0);
            if (n < 0) {
                logger_.Error("recv() failed");
                break;
            }

            if (n == 0) {
                logger_.Info("client disconnected");
                break;
            }

            input_buffer.append(temp, n);

            while (true) {
                if (input_buffer.size() >= kHeaderLen) {
                    std::uint32_t net_len = 0;
                    std::memcpy(&net_len, input_buffer.data(), kHeaderLen);
                    std::uint32_t body_len = ntohl(net_len);
                    if (body_len > kMaxBodyLen) {
                        logger_.Error("invalid frame: body too large");
                        close(conn_fd);
                        conn_fd = -1;
                        break;
                    }
                }

                std::string payload;
                if (!TryParseFrame(input_buffer, payload)) {
                    break;
                }

                logger_.Info("frame payload: " + payload);

                std::string reply = "server: " + payload;
                if (!SendFrame(conn_fd, reply)) {
                    close(conn_fd);
                    conn_fd = -1;
                    break;
                }
            }

            if (conn_fd < 0) {
                break;
            }
        }

        if (conn_fd >= 0) {
            close(conn_fd);
        }
    }
}


void TcpServer::Stop(){
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        logger_.Info("server stopped");
    }
}