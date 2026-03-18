#ifndef TCPSERVER_H
#define TCPSERVER_H

#include "Logger.h"

class TcpServer{
    public:
        explicit TcpServer(int port);

        bool Start();
        void Run();
        void Stop();

    private:
        int port_;
        int listen_fd_;
        Logger logger_;
};
#endif