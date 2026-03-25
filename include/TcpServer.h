#ifndef TCPSERVER_H
#define TCPSERVER_H

#include "Logger.h"
#include "ScopedFd.h"

#include <cstdint>
#include <string>
#include <unordered_map>

class TcpServer{
    public:
        explicit TcpServer(int port);

        bool Start();
        void Run();
        void Stop();


    private:
        struct Connection {
            std::string peer;
            std::string input_buffer;
            std::string output_buffer;
        };

        enum class ParseResult {
            kIncomplete,
            kComplete,
            kInvalid
        };

        struct MessageFrame {
            std::uint32_t cmd;
            std::uint32_t request_id;
            std::string body;
        };

        bool SetNonBlocking(int fd);
        bool SendFrame(Connection& connection, const MessageFrame& frame);
        ParseResult TryParseFrame(std::string& input_buffer, MessageFrame& frame);
        MessageFrame ProcessMessage(const Connection& connection, const MessageFrame& request);
        bool HandleRead(int conn_fd, Connection& connection);
        bool HandleWrite(int conn_fd, Connection& connection);
        bool InitEventLoop();
        bool UpdateConnectionEvents(int conn_fd, const Connection& connection);
        void AcceptNewConnections();
        void CloseConnection(int conn_fd);

    private:
        static constexpr std::uint32_t kHeaderLen = 12;
        static constexpr std::uint32_t kMaxBodyLen = 4096;


        int port_;
        ScopedFd listen_fd_;
        Logger logger_;
        class EventLoop* event_loop_;
        std::unordered_map<int, Connection> connections_;
};
#endif
