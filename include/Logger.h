#ifndef LOGGER_H
#define LOGGER_H
#include <string>
class Logger{
    public:
        void Info(const std::string&message);
        void Error(const std::string&message);
    private:
        std::string GetCurrentTime();
};
#endif