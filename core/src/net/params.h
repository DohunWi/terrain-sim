#pragma once
#include <string>
#include <unordered_map>

std::unordered_map<std::string, std::string> parseParams(const std::string& text);

// parseParams()가 돌려주는 map에서 타입별로 값을 꺼내는 헬퍼.
// tune_cli.cpp의 로컬 getFloat/getInt/getStr과 같은 역할 — 여긴 net/ 쪽
// (서버 요청 처리)에서 재사용하려고 공개 함수로 뺀 것.
float getFloat(const std::unordered_map<std::string, std::string>& params, const std::string& key, float def);
int getInt(const std::unordered_map<std::string, std::string>& params, const std::string& key, int def);
std::string getStr(const std::unordered_map<std::string, std::string>& params, const std::string& key, const std::string& def);