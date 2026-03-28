#include <iostream>
#include <algorithm>
#include <cctype>

#include "Config.h"
#include "Logger.h"
#include "TcpServer.h"

namespace {
std::string NormalizeTriggerMode(std::string mode_value) {
    std::transform(mode_value.begin(), mode_value.end(), mode_value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return mode_value;
}

bool IsValidTriggerMode(std::string mode_value) {
    mode_value = NormalizeTriggerMode(mode_value);
    return mode_value == "LT" || mode_value == "ET";
}

bool IsEdgeTriggeredMode(const std::string& mode_value) {
    return mode_value == "ET";
}
}

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
    std::string trigger_mode = NormalizeTriggerMode(config.GetString("epoll_trigger_mode", "LT"));

    Logger::Init(log_level);
    if (!IsValidTriggerMode(trigger_mode)) {
        Logger::Error("invalid epoll_trigger_mode=" + trigger_mode + ", fallback to LT");
        trigger_mode = "LT";
    }

    Logger::Info("config loaded from " + loaded_path +
                 ", port=" + std::to_string(port) +
                 ", log_level=" + log_level +
                 ", epoll_trigger_mode=" + trigger_mode);

    TcpServer server(port, IsEdgeTriggeredMode(trigger_mode));
    if (!server.Start()) {
        Logger::Error("server start failed");
        return 1;
    }

    server.Run();
    return 0;
}
