#ifndef CONFIG_H
#define CONFIG_H
#include <string>
#include <unordered_map>
class Config{
    public:
    bool Load(const std::string &filename);
    int GetInt(const std::string&key,int default_value)const;
    std::string GetString(const std::string &key,const std::string& default_value)const;

    private:
    std::unordered_map<std::string,std::string> data_;
    static std::string Trim(const std::string &s);
};
#endif