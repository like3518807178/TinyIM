#include <iostream>

#include "Config.h"
#include "Logger.h"
#include "TcpServer.h"

int main() {
    Config config;
    if (!config.Load("../conf/server.conf")) {
        std::cerr << "failed to load config file: conf/server.conf\n";
        return 1;
    }

    int port = config.GetInt("port", 8888);
    std::string log_level = config.GetString("log_level", "INFO");

    Logger::Init(log_level);
    Logger::Info("config loaded, port=" + std::to_string(port) + ", log_level=" + log_level);

    TcpServer server(port);
    if (!server.Start()) {
        Logger::Error("server start failed");
        return 1;
    }

    server.Run();
    return 0;
}