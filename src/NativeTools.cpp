#include "NativeTools.hpp"
#include "ToolRegistry.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <memory>

namespace fs = std::filesystem;

namespace agent::core {
namespace {
fs::path g_workspaceRoot = fs::current_path();

bool resolveWorkspacePath(const std::string& input, fs::path& resolved, std::string& error) {
    if (input.empty()) {
        error = "Path não fornecido.";
        return false;
    }
    try {
        fs::path candidate = fs::path(input);
        if (candidate.is_relative()) {
            candidate = g_workspaceRoot / candidate;
        }
        fs::path weak = fs::weakly_canonical(candidate);
        fs::path root = fs::weakly_canonical(g_workspaceRoot);

        auto weakStr = weak.string();
        auto rootStr = root.string();
        if (weakStr != rootStr && weakStr.rfind(rootStr + fs::path::preferred_separator, 0) != 0) {
            error = "Acesso negado: path fora do workspace.";
            return false;
        }
        resolved = weak;
        return true;
    } catch (const std::exception& e) {
        error = std::string("Path inválido: ") + e.what();
        return false;
    }
}
} // namespace

// --- FILESYSTEM TOOLS ---

std::string read_file(const nlohmann::json& args) {
    std::string path = args.value("path", "");
    fs::path resolved;
    std::string error;
    if (!resolveWorkspacePath(path, resolved, error)) return "Erro: " + error;
    
    std::ifstream ifs(resolved);
    if (!ifs.is_open()) return "Erro: Não foi possível abrir " + resolved.string();
    
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    return buffer.str();
}

std::string write_file(const nlohmann::json& args) {
    std::string path = args.value("path", "");
    std::string content = args.value("content", "");
    fs::path resolved;
    std::string error;
    if (!resolveWorkspacePath(path, resolved, error)) return "Erro: " + error;

    try {
        if (resolved.has_parent_path()) fs::create_directories(resolved.parent_path());
        
        std::ofstream ofs(resolved);
        if (!ofs.is_open()) return "Erro: Falha ao criar arquivo " + resolved.string();
        ofs << content;
        return "Sucesso: Arquivo " + resolved.string() + " gravado.";
    } catch (const std::exception& e) {
        return "Erro: " + std::string(e.what());
    }
}

std::string list_dir(const nlohmann::json& args) {
    std::string path = args.value("path", g_workspaceRoot.string());
    fs::path resolved;
    std::string error;
    if (!resolveWorkspacePath(path, resolved, error)) return "Erro: " + error;

    try {
        std::stringstream ss;
        for (const auto& entry : fs::directory_iterator(resolved)) {
            ss << entry.path().filename().string() << (entry.is_directory() ? "/" : "") << "\n";
        }
        return ss.str();
    } catch (const std::exception& e) {
        return "Erro ao listar: " + std::string(e.what());
    }
}

// --- SYSTEM TOOLS ---

std::string run_command(const nlohmann::json& args) {
    std::string command = args.value("command", "");
    if (command.empty()) return "Erro: Comando vazio.";

    std::string cmdPrefix = "cd \"" + g_workspaceRoot.string() + "\" && ";
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen((cmdPrefix + command + " 2>&1").c_str(), "r"), pclose);
    if (!pipe) return "Erro: Falha ao executar popen()";
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result.empty() ? "(Comando executado sem saída)" : result;
}

std::string grep_search(const nlohmann::json& args) {
    std::string pattern = args.value("pattern", "");
    std::string path = args.value("path", ".");
    if (pattern.empty()) return "Erro: pattern não fornecido.";
    fs::path resolved;
    std::string error;
    if (!resolveWorkspacePath(path, resolved, error)) return "Erro: " + error;

    std::string command = "cd \"" + g_workspaceRoot.string() + "\" && rg -n --hidden --glob '!.git' \"" +
                          pattern + "\" \"" + resolved.string() + "\" 2>&1";

    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) return "Erro: Falha ao executar rg";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result.empty() ? "Nenhum resultado." : result;
}

std::string read_file_slice(const nlohmann::json& args) {
    std::string path = args.value("path", "");
    int fromLine = args.value("from_line", 1);
    int toLine = args.value("to_line", 200);
    if (fromLine < 1) fromLine = 1;
    if (toLine < fromLine) return "Erro: intervalo inválido.";

    fs::path resolved;
    std::string error;
    if (!resolveWorkspacePath(path, resolved, error)) return "Erro: " + error;

    std::ifstream ifs(resolved);
    if (!ifs.is_open()) return "Erro: Não foi possível abrir " + resolved.string();

    std::stringstream out;
    std::string line;
    int idx = 0;
    while (std::getline(ifs, line)) {
        ++idx;
        if (idx < fromLine) continue;
        if (idx > toLine) break;
        out << idx << ": " << line << "\n";
    }
    return out.str().empty() ? "Sem conteúdo no intervalo solicitado." : out.str();
}

void registerNativeTools(const std::string& workspaceRoot) {
    try {
        g_workspaceRoot = fs::weakly_canonical(fs::path(workspaceRoot.empty() ? "." : workspaceRoot));
    } catch (...) {
        g_workspaceRoot = fs::current_path();
    }

    auto& reg = ToolRegistry::instance();

    reg.registerTool("read_file", "Lê o conteúdo de um arquivo.", {"path"}, read_file);
    reg.registerTool("read_file_slice", "Lê intervalo de linhas de um arquivo.", {"path", "from_line", "to_line"}, read_file_slice);
    reg.registerTool("write_file", "Grava conteúdo em um arquivo.", {"path", "content"}, write_file);
    reg.registerTool("list_dir", "Lista arquivos de um diretório.", {"path"}, list_dir);
    reg.registerTool("grep_search", "Busca padrão textual com ripgrep no workspace.", {"pattern", "path"}, grep_search);
    reg.registerTool("run_command", "Executa um comando no shell e retorna a saída.", {"command"}, run_command);
}

} // namespace agent::core
