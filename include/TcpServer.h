#ifndef TCPSERVER_H
#define TCPSERVER_H

#include "Logger.h"

#include <cstdint>
#include <string>

class TcpServer{
    public:
        explicit TcpServer(int port);

        bool Start();
        void Run();
        void Stop();


    private:
        bool SendFrame(int conn_fd, const std::string& payload);
        bool TryParseFrame(std::string& input_buffer, std::string& payload);

    private:
        static constexpr std::uint32_t kHeaderLen = 4;
        static constexpr std::uint32_t kMaxBodyLen = 4096;


        int port_;
        int listen_fd_;
        Logger logger_;
};
#endif