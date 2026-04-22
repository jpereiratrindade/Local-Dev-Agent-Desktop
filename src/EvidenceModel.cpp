#include "EvidenceModel.hpp"
#include "json.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <sys/wait.h>

namespace fs = std::filesystem;

namespace agent::core {
namespace {

std::string shellEscape(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

fs::path resolvePath(const std::string& workspaceRoot, const std::string& path) {
    fs::path candidate(path);
    if (candidate.is_relative()) candidate = fs::path(workspaceRoot) / candidate;
    return fs::weakly_canonical(candidate);
}

EvidenceType parseEvidenceType(const std::string& raw) {
    std::string value = raw;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (value == "file_exists") return EvidenceType::FileExists;
    if (value == "file_contains") return EvidenceType::FileContains;
    if (value == "directory_contains") return EvidenceType::DirectoryContains;
    if (value == "command_succeeds") return EvidenceType::CommandSucceeds;
    return EvidenceType::Unknown;
}

bool commandSucceeds(const std::string& workspaceRoot, const std::string& command, std::string& observation) {
    if (command.empty()) {
        observation = "Comando vazio.";
        return false;
    }
    static const std::regex privileged(R"((^|[;&|()\s])(sudo|pkexec|su)(\s|$))");
    if (std::regex_search(command, privileged)) {
        observation = "Comando de evidência bloqueado por exigir privilégios ou senha interativa.";
        return false;
    }

    std::string wrapped = "cd " + shellEscape(workspaceRoot) + " && timeout 60s sh -c " +
                          shellEscape(command) + " >/tmp/agent_evidence_command.out 2>&1";
    int rc = std::system(wrapped.c_str());
    std::string output = readFile("/tmp/agent_evidence_command.out");
    if (output.size() > 800) output = output.substr(0, 800) + "\n...[saida truncada]...";

    if (rc == -1) {
        observation = "Falha ao executar comando.";
        return false;
    }
    int exitCode = WIFEXITED(rc) ? WEXITSTATUS(rc) : rc;
    observation = "exit_code=" + std::to_string(exitCode);
    if (!output.empty()) observation += "\n" + output;
    return exitCode == 0;
}

bool parseJsonObject(const std::string& candidate, nlohmann::json& out) {
    try {
        out = nlohmann::json::parse(candidate);
        return out.is_object();
    } catch (...) {
        return false;
    }
}

bool extractTaggedJson(const std::string& text, nlohmann::json& out) {
    std::regex tagRegex("<evidence_model>\\s*([\\s\\S]*?)\\s*</evidence_model>");
    std::smatch match;
    if (std::regex_search(text, match, tagRegex) && parseJsonObject(match[1].str(), out)) return true;

    std::regex blockRegex("```json\\s*([\\s\\S]*?)\\s*```");
    for (std::sregex_iterator it(text.begin(), text.end(), blockRegex), end; it != end; ++it) {
        nlohmann::json candidate;
        if (!parseJsonObject((*it)[1].str(), candidate)) continue;
        if (candidate.contains("evidence") || candidate.contains("items")) {
            out = candidate;
            return true;
        }
    }
    return false;
}

} // namespace

std::string evidenceTypeName(EvidenceType type) {
    switch (type) {
        case EvidenceType::FileExists: return "file_exists";
        case EvidenceType::FileContains: return "file_contains";
        case EvidenceType::DirectoryContains: return "directory_contains";
        case EvidenceType::CommandSucceeds: return "command_succeeds";
        default: return "unknown";
    }
}

EvidenceModel parseEvidenceModelFromText(const std::string& text, const std::string& fallbackGoal) {
    EvidenceModel model;
    model.goal = fallbackGoal;

    nlohmann::json root;
    if (!extractTaggedJson(text, root)) return model;

    model.goal = root.value("goal", fallbackGoal);
    nlohmann::json items = root.contains("evidence") ? root["evidence"] : root.value("items", nlohmann::json::array());
    if (!items.is_array()) return model;

    for (const auto& raw : items) {
        if (!raw.is_object()) continue;
        EvidenceItem item;
        item.type = parseEvidenceType(raw.value("type", ""));
        item.path = raw.value("path", raw.value("file", raw.value("directory", "")));
        item.text = raw.value("text", raw.value("containsText", raw.value("pattern", "")));
        item.command = raw.value("command", "");
        item.required = raw.value("required", true);
        if (item.type != EvidenceType::Unknown) model.items.push_back(item);
    }
    return model;
}

void refreshEvidence(EvidenceModel& model, const std::string& workspaceRoot) {
    for (auto& item : model.items) {
        item.satisfied = false;
        try {
            if (item.type == EvidenceType::FileExists) {
                fs::path resolved = resolvePath(workspaceRoot, item.path);
                item.satisfied = fs::exists(resolved) && fs::is_regular_file(resolved);
                item.lastObservation = item.satisfied ? "Arquivo existe: " + resolved.string()
                                                      : "Arquivo ausente: " + resolved.string();
            } else if (item.type == EvidenceType::FileContains) {
                fs::path resolved = resolvePath(workspaceRoot, item.path);
                std::string content = readFile(resolved);
                item.satisfied = !item.text.empty() && content.find(item.text) != std::string::npos;
                item.lastObservation = item.satisfied ? "Texto encontrado em " + resolved.string()
                                                      : "Texto esperado não encontrado em " + resolved.string();
            } else if (item.type == EvidenceType::DirectoryContains) {
                fs::path resolved = resolvePath(workspaceRoot, item.path);
                bool hasEntry = false;
                if (fs::exists(resolved) && fs::is_directory(resolved)) {
                    if (item.text.empty()) {
                        hasEntry = !fs::is_empty(resolved);
                    } else {
                        for (const auto& entry : fs::directory_iterator(resolved)) {
                            if (entry.path().filename().string().find(item.text) != std::string::npos) {
                                hasEntry = true;
                                break;
                            }
                        }
                    }
                }
                item.satisfied = hasEntry;
                item.lastObservation = item.satisfied ? "Diretório contém evidência esperada: " + resolved.string()
                                                      : "Diretório não contém evidência esperada: " + resolved.string();
            } else if (item.type == EvidenceType::CommandSucceeds) {
                item.satisfied = commandSucceeds(workspaceRoot, item.command, item.lastObservation);
            }
        } catch (const std::exception& e) {
            item.satisfied = false;
            item.lastObservation = std::string("Erro ao verificar evidência: ") + e.what();
        }
    }
}

bool isSatisfied(const EvidenceModel& model) {
    if (model.items.empty()) return false;
    for (const auto& item : model.items) {
        if (item.required && !item.satisfied) return false;
    }
    return true;
}

bool isWeakEvidence(const EvidenceModel& model) {
    if (model.items.empty()) return true;
    bool hasContentEvidence = false;
    bool hasCommandEvidence = false;
    for (const auto& item : model.items) {
        if (item.type == EvidenceType::FileContains || item.type == EvidenceType::DirectoryContains) {
            hasContentEvidence = true;
        }
        if (item.type == EvidenceType::CommandSucceeds) {
            hasCommandEvidence = true;
        }
    }
    return !hasContentEvidence && !hasCommandEvidence;
}

std::string summarizeEvidence(const EvidenceModel& model) {
    if (model.items.empty()) return "Modelo de evidência ausente.";
    std::stringstream out;
    out << "EvidenceModel: " << (model.goal.empty() ? "(sem descrição)" : model.goal) << "\n";
    for (const auto& item : model.items) {
        out << "- [" << (item.satisfied ? "ok" : "pendente") << "] "
            << evidenceTypeName(item.type);
        if (!item.path.empty()) out << " path=" << item.path;
        if (!item.text.empty()) out << " text=\"" << item.text << "\"";
        if (!item.command.empty()) out << " command=\"" << item.command << "\"";
        if (!item.lastObservation.empty()) out << " :: " << item.lastObservation;
        out << "\n";
    }
    return out.str();
}

} // namespace agent::core
