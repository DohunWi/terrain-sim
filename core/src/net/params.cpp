#include "params.h"
#include <sstream>

std::unordered_map<std::string, std::string> parseParams(const std::string& text) {
    std::unordered_map<std::string, std::string> params;
    std::istringstream iss(text);
    std::string line;

    while (std::getline(iss, line, '\n')) {
        // 여기 채우기: line을 '=' 기준으로 key/value로 나눠서 params에 넣기
        size_t idx = line.find("=");
        if(idx == std::string::npos){
            continue;
        }
        std::string key = line.substr(0,idx);
        std::string value = line.substr(idx+1);
        params[key] = value;
    }

    return params;
}

float getFloat(const std::unordered_map<std::string, std::string>& params, const std::string& key, float def) {
    auto it = params.find(key);
    return it != params.end() ? std::stof(it->second) : def;
}

int getInt(const std::unordered_map<std::string, std::string>& params, const std::string& key, int def) {
    auto it = params.find(key);
    return it != params.end() ? std::stoi(it->second) : def;
}

std::string getStr(const std::unordered_map<std::string, std::string>& params, const std::string& key, const std::string& def) {
    auto it = params.find(key);
    return it != params.end() ? it->second : def;
}