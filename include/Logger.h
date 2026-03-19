#ifndef LOGGER_H
#define LOGGER_H
#include <string>
class Logger{
    public:
        static void Init(const std::string&level);
        static void Info(const std::string&message);
        static void Error(const std::string&message);
    private:
        static std::string GetCurrentTime();
        static std::string level_;
};
#endif
