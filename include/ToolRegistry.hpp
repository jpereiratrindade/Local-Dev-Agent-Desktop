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
    bool mutatesWorkspace = false;
    bool verifiesState = false;
};

class ToolRegistry {
public:
    static ToolRegistry& instance();

    void registerTool(const std::string& name, const std::string& description, 
                      const std::vector<std::string>& argNames, ToolFunc func);
    void registerTool(const std::string& name, const std::string& description,
                      const std::vector<std::string>& argNames, ToolFunc func,
                      bool mutatesWorkspace, bool verifiesState);

    std::string dispatch(const std::string& name, const nlohmann::json& args);
    std::string getToolSpecs() const;
    bool toolMutatesWorkspace(const std::string& name) const;
    bool toolVerifiesState(const std::string& name) const;

private:
    ToolRegistry() = default;
    std::map<std::string, ToolInfo> tools;
};

} // namespace agent::core
