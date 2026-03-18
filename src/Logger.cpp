#include "Logger.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

std::string Logger::GetCurrentTime(){
    auto now=std::chrono::system_clock::now();
    std::time_t now_time=std::chrono::system_clock::to_time_t(now);

    std::tm local_tm{};
    localtime_r(&now_time, &local_tm);

    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void Logger::Info(const std::string& message) {
    std::cout << "[" << GetCurrentTime() << "] "
              << "[INFO] "
              << message << std::endl;
}

void Logger::Error(const std::string& message) {
    std::cerr << "[" << GetCurrentTime() << "] "
              << "[ERROR] "
              << message << std::endl;
}