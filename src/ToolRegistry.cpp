#include "ToolRegistry.hpp"
#include <sstream>

namespace agent::core {

ToolRegistry& ToolRegistry::instance() {
    static ToolRegistry inst;
    return inst;
}

void ToolRegistry::registerTool(const std::string& name, const std::string& description, 
                               const std::vector<std::string>& argNames, ToolFunc func) {
    tools[name] = {name, description, argNames, func};
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
    std::stringstream ss;
    for (const auto& [name, info] : tools) {
        ss << "### " << name << "\n";
        ss << "Descrição: " << info.description << "\n";
        ss << "Argumentos: ";
        for (size_t i = 0; i < info.argNames.size(); ++i) {
            ss << info.argNames[i] << (i == info.argNames.size() - 1 ? "" : ", ");
        }
        ss << "\n\n";
    }
    return ss.str();
}

} // namespace agent::core
