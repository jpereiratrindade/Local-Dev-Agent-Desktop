#pragma once

#include <string>
#include <map>
#include <functional>
#include <vector>
#include "json.hpp"

namespace agent::core {

using ToolFunc = std::function<std::string(const nlohmann::json&)>;

struct ToolInfo {
    std::string name;
    std::string description;
    std::vector<std::string> argNames; // Simplificado para o prompt
    ToolFunc func;
};

class ToolRegistry {
public:
    static ToolRegistry& instance();

    void registerTool(const std::string& name, const std::string& description, 
                      const std::vector<std::string>& argNames, ToolFunc func);

    std::string dispatch(const std::string& name, const nlohmann::json& args);
    std::string getToolSpecs() const;

private:
    ToolRegistry() = default;
    std::map<std::string, ToolInfo> tools;
};

} // namespace agent::core
