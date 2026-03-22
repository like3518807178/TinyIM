#include <iostream>

#include "Config.h"
#include "Logger.h"
#include "TcpServer.h"

int main() {
    Config config;
    const char* config_paths[] = {
        "conf/server.conf",
        "../conf/server.conf"
    };

    bool loaded = false;
    std::string loaded_path;
    for (const char* path : config_paths) {
        if (config.Load(path)) {
            loaded = true;
            loaded_path = path;
            break;
        }
    }

    if (!loaded) {
        std::cerr << "failed to load config file: conf/server.conf or ../conf/server.conf\n";
        return 1;
    }

    int port = config.GetInt("port", 8888);
    std::string log_level = config.GetString("log_level", "INFO");

    Logger::Init(log_level);
    Logger::Info("config loaded from " + loaded_path +
                 ", port=" + std::to_string(port) +
                 ", log_level=" + log_level);

    TcpServer server(port);
    if (!server.Start()) {
        Logger::Error("server start failed");
        return 1;
    }

    server.Run();
    return 0;
}
