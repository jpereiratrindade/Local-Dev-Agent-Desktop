#include "AgentUI_Internal.hpp"
#include "json.hpp"
#include <fstream>
#include <algorithm>

namespace agent::ui {

namespace {
const char* messagePartTypeNamePersist(MessagePartType type) {
    switch (type) {
        case MessagePartType::Reasoning: return "reasoning";
        case MessagePartType::ToolCall: return "tool_call";
        case MessagePartType::ToolResult: return "tool_result";
        case MessagePartType::Text:
        default: return "text";
    }
}

MessagePartType messagePartTypeFromStringPersist(const std::string& value) {
    if (value == "reasoning") return MessagePartType::Reasoning;
    if (value == "tool_call") return MessagePartType::ToolCall;
    if (value == "tool_result") return MessagePartType::ToolResult;
    return MessagePartType::Text;
}
}

std::filesystem::path AgentUI::sessionsDir() const {
    fs::path p = fs::path(currentProjectRoot) / ".agent" / "sessions";
    return p;
}

std::vector<std::pair<fs::path, fs::file_time_type>> AgentUI::listRecentSessions(std::size_t maxCount) const {
    std::vector<std::pair<fs::path, fs::file_time_type>> result;
    try {
        fs::path sdir = sessionsDir();
        if (!fs::exists(sdir)) return result;
        for (const auto& entry : fs::directory_iterator(sdir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                result.push_back({entry.path(), entry.last_write_time()});
            }
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });
        if (result.size() > maxCount) result.resize(maxCount);
    } catch (...) {}
    return result;
}

bool AgentUI::loadSessionFromFile(const fs::path& path) {
    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return false;
        nlohmann::json j;
        ifs >> j;
        if (j.contains("history")) {
            std::lock_guard<std::mutex> lock(msgMutex);
            history.clear();
            structuredHistory.clear();
            for (const auto& item : j["history"]) {
                ChatMessage message{item["role"], item["text"]};
                history.push_back(message);
                if (item.contains("parts") && item["parts"].is_array()) {
                    StructuredChatMessage structured;
                    structured.role = message.role;
                    for (const auto& part : item["parts"]) {
                        MessagePart parsed;
                        parsed.type = messagePartTypeFromStringPersist(part.value("type", "text"));
                        parsed.text = part.value("text", "");
                        parsed.name = part.value("name", "");
                        parsed.callId = part.value("call_id", "");
                        parsed.isError = part.value("is_error", false);
                        structured.parts.push_back(parsed);
                    }
                    if (structured.parts.empty()) {
                        structured = buildStructuredMessage(message);
                    }
                    structuredHistory.push_back(structured);
                } else {
                    structuredHistory.push_back(buildStructuredMessage(message));
                }
            }
            currentSessionFile = path.filename().string();
            return true;
        }
    } catch (...) {}
    return false;
}

void AgentUI::saveSession() {
    if (!hasOpenProject || currentProjectRoot.empty()) return;
    if (history.empty()) return;
    try {
        fs::path sdir = sessionsDir();
        if (!fs::exists(sdir)) fs::create_directories(sdir);
        
        nlohmann::json j;
        j["history"] = nlohmann::json::array();
        {
            std::lock_guard<std::mutex> lock(msgMutex);
            if (structuredHistory.size() != history.size()) {
                structuredHistory.clear();
                for (const auto& msg : history) {
                    structuredHistory.push_back(buildStructuredMessage(msg));
                }
            }
            for (size_t i = 0; i < history.size(); ++i) {
                const auto& msg = history[i];
                nlohmann::json item = {{"role", msg.role}, {"text", msg.text}};
                item["parts"] = nlohmann::json::array();
                if (i < structuredHistory.size()) {
                    for (const auto& part : structuredHistory[i].parts) {
                        item["parts"].push_back({
                            {"type", messagePartTypeNamePersist(part.type)},
                            {"text", part.text},
                            {"name", part.name},
                            {"call_id", part.callId},
                            {"is_error", part.isError},
                        });
                    }
                }
                j["history"].push_back(item);
            }
        }
        std::ofstream ofs(sdir / currentSessionFile);
        ofs << j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (...) {}
}

void AgentUI::loadSession() {
    if (!hasOpenProject || currentProjectRoot.empty()) return;

    fs::path preferred = sessionsDir() / currentSessionFile;
    if (fs::exists(preferred) && loadSessionFromFile(preferred)) return;

    auto recent = listRecentSessions(1);
    if (!recent.empty()) loadSessionFromFile(recent.front().first);
}

} // namespace agent::ui
