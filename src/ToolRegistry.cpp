#include "ToolRegistry.hpp"
#include <sstream>

namespace agent::core {
namespace {

bool toolAllowedByProfile(const ToolInfo& info, const std::string& toolProfile) {
    if (toolProfile.empty() || toolProfile == "workspace" || toolProfile == "full-access") return true;
    if (toolProfile == "read-only" || toolProfile == "inspect") {
        return !info.mutatesWorkspace;
    }
    if (toolProfile == "coding") {
        return true;
    }
    if (toolProfile == "writing") {
        return info.name == "read_file" ||
               info.name == "read_file_slice" ||
               info.name == "write_file" ||
               info.name == "apply_patch" ||
               info.name == "list_dir" ||
               info.name == "grep_search" ||
               info.name == "search_library";
    }
    if (toolProfile == "research") {
        return info.name == "read_file" ||
               info.name == "read_file_slice" ||
               info.name == "list_dir" ||
               info.name == "grep_search" ||
               info.name == "fetch_url" ||
               info.name == "search_library" ||
               info.name == "rag_cache_status" ||
               info.name == "ingest_document";
    }
    return true;
}

template <typename Writer>
void forEachAllowedTool(const std::map<std::string, ToolInfo>& tools, const std::string& toolProfile, Writer writer) {
    for (const auto& [name, info] : tools) {
        if (!toolAllowedByProfile(info, toolProfile)) continue;
        writer(name, info);
    }
}

}

ToolRegistry& ToolRegistry::instance() {
    static ToolRegistry inst;
    return inst;
}

void ToolRegistry::registerTool(const std::string& name, const std::string& description, 
                               const std::vector<std::string>& argNames, ToolFunc func) {
    registerTool(name, description, argNames, func, false, false);
}

void ToolRegistry::registerTool(const std::string& name, const std::string& description,
                               const std::vector<std::string>& argNames, ToolFunc func,
                               bool mutatesWorkspace, bool verifiesState) {
    nlohmann::json properties = nlohmann::json::object();
    for (const auto& argName : argNames) {
        properties[argName] = {
            {"type", "string"},
            {"description", "Argumento '" + argName + "'."}
        };
    }

    nlohmann::json fallbackSchema = {
        {"type", "object"},
        {"properties", properties}
    };

    if (!argNames.empty()) {
        fallbackSchema["required"] = argNames;
    }

    registerTool(name, description, argNames, fallbackSchema, func, mutatesWorkspace, verifiesState);
}

void ToolRegistry::registerTool(const std::string& name, const std::string& description,
                               const std::vector<std::string>& argNames, const nlohmann::json& parametersSchema,
                               ToolFunc func, bool mutatesWorkspace, bool verifiesState) {
    tools[name] = {name, description, argNames, parametersSchema, func, mutatesWorkspace, verifiesState};
}

std::string ToolRegistry::dispatch(const std::string& name, const nlohmann::json& args) {
    auto it = tools.find(name);
    if (it == tools.end()) {
        return "ERRO: Ferramenta '" + name + "' não encontrada.";
    }
    
    try {
        return it->second.func(args);
    } catch (const std::exception& e) {
        return "ERRO ao executar '" + name + "': " + std::string(e.what());
    } catch (...) {
        return "ERRO fatal ao executar '" + name + "'.";
    }
}

std::string ToolRegistry::getToolSpecs() const {
    return getToolSpecsForProfile("");
}

std::string ToolRegistry::getToolSpecsForProfile(const std::string& toolProfile) const {
    std::stringstream ss;
    forEachAllowedTool(tools, toolProfile, [&](const auto& name, const ToolInfo& info) {
        ss << "### " << name << "\n";
        ss << "Descrição: " << info.description << "\n";
        ss << "Argumentos: ";
        for (size_t i = 0; i < info.argNames.size(); ++i) {
            ss << info.argNames[i] << (i == info.argNames.size() - 1 ? "" : ", ");
        }
        ss << "\n\n";
    });
    return ss.str();
}

std::vector<std::string> ToolRegistry::listToolNamesForProfile(const std::string& toolProfile) const {
    std::vector<std::string> names;
    forEachAllowedTool(tools, toolProfile, [&](const auto& name, const ToolInfo&) {
        names.push_back(name);
    });
    return names;
}

nlohmann::json ToolRegistry::getOpenAiToolSpecs() const {
    return getOpenAiToolSpecsForProfile("");
}

nlohmann::json ToolRegistry::getOpenAiToolSpecsForProfile(const std::string& toolProfile) const {
    nlohmann::json specs = nlohmann::json::array();
    forEachAllowedTool(tools, toolProfile, [&](const auto& name, const ToolInfo& info) {
        specs.push_back({
            {"type", "function"},
            {"function", {
                {"name", info.name},
                {"description", info.description},
                {"parameters", info.parametersSchema.is_null() ? nlohmann::json::object() : info.parametersSchema}
            }}
        });
    });
    return specs;
}

bool ToolRegistry::toolAllowedForProfile(const std::string& name, const std::string& toolProfile) const {
    auto it = tools.find(name);
    return it != tools.end() && toolAllowedByProfile(it->second, toolProfile);
}

bool ToolRegistry::toolMutatesWorkspace(const std::string& name) const {
    auto it = tools.find(name);
    return it != tools.end() && it->second.mutatesWorkspace;
}

bool ToolRegistry::toolVerifiesState(const std::string& name) const {
    auto it = tools.find(name);
    return it != tools.end() && it->second.verifiesState;
}

} // namespace agent::core
