#pragma once
#include <string>
#include <nlohmann/json.hpp>

// Two-pass JSON auto-repair and balanced-bracket AST fallback recovery engine
// for salvaging partial or malformed LLM JSON outputs.
class JsonRepairEngine {
public:
    static std::string autoRepairJson(const std::string& raw);
    static std::string extractBalancedJson(const std::string& str, size_t startPos, char openChar, char closeChar);
    static nlohmann::json parseOrRecoverJson(const std::string& raw);
};

