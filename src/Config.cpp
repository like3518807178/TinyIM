#include "Config.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

bool Config::Load(const std::string &filename){
    std::ifstream ifs(filename);
    if(!ifs.is_open()){
        return false;
    }

    std::string line;
    while(std::getline(ifs,line)){
        line=Trim(line);
        if(line.empty())continue;
        if(line[0]=='#')continue;

        std::size_t pos=line.find('=');
        if(pos==std::string::npos)continue;

        std::string key=Trim(line.substr(0,pos));
        std::string value =Trim(line.substr(pos+1));

        if(!key.empty()){
            data_[key]=value;
        }
    }
    return true;
}

int Config::GetInt(const std::string &key,int default_value)const{
    auto it=data_.find(key);
    if(it==data_.end()){
        return default_value;
    }

    try{
        return std::stoi(it->second);
    } catch(...){
        return default_value;
    }
}

std::string Config::GetString(const std::string &key,const std::string &default_value)const{
    auto it=data_.find(key);
    if(it==data_.end()){
        return default_value;
    }
    return it->second;
}


std::string Config::Trim(const std::string&s){
    std::size_t left=0;
    while(left<s.size()&&std::isspace(static_cast<unsigned char>(s[left]))){
        ++left;
    }

    std::size_t right=s.size();
    while(right>left&&std::isspace(static_cast<unsigned char>(s[right-1]))){
        --right;
    }
    return s.substr(left,right-left);
}
