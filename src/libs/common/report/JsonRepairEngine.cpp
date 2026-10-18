#include "JsonRepairEngine.h"
#include <regex>
#include <vector>

std::string JsonRepairEngine::autoRepairJson(const std::string& raw)
{
    size_t firstBrace = raw.find('{');
    if (firstBrace == std::string::npos) {
        return "{}";
    }

    std::string s = raw.substr(firstBrace);
    size_t fence = s.find("```");
    if (fence != std::string::npos) {
        s = s.substr(0, fence);
    }

    // Pass 1: Clean syntax anomalies inside non-string regions:
    // - ": ," -> ": null,"
    // - ": -" (dash used in table cell as null/empty) -> ": null"
    // - ": NA" or ": N/A" -> ": null"
    // - Trailing commas before '}' or ']'
    std::string cleaned;
    cleaned.reserve(s.size() + 64);
    bool inString = false;
    bool escapeNext = false;

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (inString) {
            if (escapeNext) {
                escapeNext = false;
            } else if (c == '\\') {
                escapeNext = true;
            } else if (c == '"') {
                inString = false;
            }
            cleaned += c;
        } else {
            if (c == '"') {
                inString = true;
                cleaned += c;
            } else if (c == ':') {
                cleaned += c;
                size_t j = i + 1;
                while (j < s.size() && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) {
                    j++;
                }
                if (j < s.size()) {
                    if (s[j] == ',') {
                        cleaned += " null";
                    } else if (s[j] == '-' && (j + 1 >= s.size() || s[j+1] == ',' || s[j+1] == '}' || s[j+1] == ']' || s[j+1] == ' ' || s[j+1] == '\r' || s[j+1] == '\n')) {
                        cleaned += " null";
                        i = j;
                        continue;
                    } else if ((s[j] == 'N' || s[j] == 'n') && j + 1 < s.size() && (s[j+1] == 'A' || s[j+1] == 'a' || s[j+1] == '/')) {
                        cleaned += " null";
                        while (j < s.size() && (s[j] == 'N' || s[j] == 'n' || s[j] == 'A' || s[j] == 'a' || s[j] == '/')) {
                            j++;
                        }
                        i = j - 1;
                        continue;
                    }
                }
            } else if (c == ',') {
                size_t j = i + 1;
                while (j < s.size() && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) {
                    j++;
                }
                if (j < s.size() && (s[j] == '}' || s[j] == ']')) {
                    continue; // Skip illegal trailing comma before } or ]
                }
                cleaned += c;
            } else {
                cleaned += c;
            }
        }
    }

    // Pass 2: Stack-based brace & bracket balancer
    std::vector<char> stack;
    inString = false;
    escapeNext = false;

    for (size_t i = 0; i < cleaned.size(); ++i) {
        char c = cleaned[i];
        if (inString) {
            if (escapeNext) {
                escapeNext = false;
            } else if (c == '\\') {
                escapeNext = true;
            } else if (c == '"') {
                inString = false;
            }
        } else {
            if (c == '"') {
                inString = true;
            } else if (c == '{') {
                stack.push_back('}');
            } else if (c == '[') {
                stack.push_back(']');
            } else if (c == '}') {
                if (!stack.empty() && stack.back() == '}') {
                    stack.pop_back();
                }
            } else if (c == ']') {
                if (!stack.empty() && stack.back() == ']') {
                    stack.pop_back();
                }
            }
        }
    }

    if (inString) {
        cleaned += '"';
    }

    // Clean up trailing comma or dangling colon before closing brackets
    size_t lastNonWs = cleaned.find_last_not_of(" \t\r\n");
    if (lastNonWs != std::string::npos) {
        if (cleaned[lastNonWs] == ',') {
            cleaned.erase(lastNonWs);
        } else if (cleaned[lastNonWs] == ':') {
            cleaned += " null";
        }
    }

    while (!stack.empty()) {
        cleaned += stack.back();
        stack.pop_back();
    }

    return cleaned;
}

std::string JsonRepairEngine::extractBalancedJson(const std::string& str, size_t startPos, char openChar, char closeChar)
{
    if (startPos >= str.size() || str[startPos] != openChar) return "";
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    size_t endPos = startPos;
    for (size_t i = startPos; i < str.size(); ++i) {
        char c = str[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\' && inString) {
            escaped = true;
            continue;
        }
        if (c == '"') {
            inString = !inString;
            continue;
        }
        if (!inString) {
            if (c == openChar) {
                depth++;
            } else if (c == closeChar) {
                depth--;
                if (depth == 0) {
                    endPos = i;
                    return str.substr(startPos, endPos - startPos + 1);
                }
            }
        }
    }
    std::string partial = str.substr(startPos);
    if (inString) partial += '"';
    while (depth-- > 0) partial += closeChar;
    return partial;
}

nlohmann::json JsonRepairEngine::parseOrRecoverJson(const std::string& raw)
{
    if (raw.empty()) {
        return nlohmann::json::object();
    }
    // 1. Try standard JSON parse on auto-repaired string
    std::string repaired = autoRepairJson(raw);
    try {
        return nlohmann::json::parse(repaired);
    } catch (...) {
        // Fall through to resilient field recovery
    }

    // 2. High-resilience scanner fallback: salvage all valid "key": value pairs
    nlohmann::json obj = nlohmann::json::object();
    static const std::regex keyRegex(R"pattern("([a-zA-Z0-9_]+)"\s*:\s*)pattern");

    auto words_begin = std::sregex_iterator(raw.begin(), raw.end(), keyRegex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator it = words_begin; it != words_end; ++it) {
        std::smatch match = *it;
        std::string key = match[1].str();
        size_t valStart = match.position() + match.length();

        while (valStart < raw.size() && (raw[valStart] == ' ' || raw[valStart] == '\t' || raw[valStart] == '\r' || raw[valStart] == '\n')) {
            valStart++;
        }
        if (valStart >= raw.size()) continue;

        char firstChar = raw[valStart];
        if (firstChar == '[') {
            std::string arrStr = extractBalancedJson(raw, valStart, '[', ']');
            try {
                obj[key] = nlohmann::json::parse(arrStr);
            } catch (...) {
                try {
                    obj[key] = nlohmann::json::parse(autoRepairJson(arrStr));
                } catch (...) {
                    obj[key] = nlohmann::json::array();
                }
            }
        } else if (firstChar == '{') {
            std::string objStr = extractBalancedJson(raw, valStart, '{', '}');
            try {
                obj[key] = nlohmann::json::parse(objStr);
            } catch (...) {
                try {
                    obj[key] = nlohmann::json::parse(autoRepairJson(objStr));
                } catch (...) {
                    obj[key] = nlohmann::json::object();
                }
            }
        } else if (firstChar == '"') {
            bool escaped = false;
            size_t strEnd = valStart + 1;
            while (strEnd < raw.size()) {
                if (escaped) {
                    escaped = false;
                } else if (raw[strEnd] == '\\') {
                    escaped = true;
                } else if (raw[strEnd] == '"') {
                    break;
                }
                strEnd++;
            }
            std::string strContent = raw.substr(valStart + 1, (strEnd < raw.size() ? strEnd : raw.size()) - (valStart + 1));
            obj[key] = strContent;
        } else if (firstChar == 't' || firstChar == 'f' || firstChar == 'n') {
            if (raw.compare(valStart, 4, "true") == 0) obj[key] = true;
            else if (raw.compare(valStart, 5, "false") == 0) obj[key] = false;
            else if (raw.compare(valStart, 4, "null") == 0) obj[key] = nullptr;
        } else if ((firstChar >= '0' && firstChar <= '9') || firstChar == '-') {
            size_t numEnd = valStart;
            while (numEnd < raw.size() && ((raw[numEnd] >= '0' && raw[numEnd] <= '9') || raw[numEnd] == '.' || raw[numEnd] == '-' || raw[numEnd] == 'e' || raw[numEnd] == 'E' || raw[numEnd] == '+')) {
                numEnd++;
            }
            std::string numStr = raw.substr(valStart, numEnd - valStart);
            try {
                if (numStr.find('.') != std::string::npos || numStr.find('e') != std::string::npos || numStr.find('E') != std::string::npos) {
                    obj[key] = std::stod(numStr);
                } else {
                    obj[key] = std::stoll(numStr);
                }
            } catch (...) {
                obj[key] = numStr;
            }
        }
    }

    return obj;
}

