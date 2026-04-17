#include "AgentUI_Internal.hpp"
#include "json.hpp"
#include <fstream>
#include <algorithm>

namespace agent::ui {

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
            for (const auto& item : j["history"]) {
                history.push_back({item["role"], item["text"]});
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
            for (const auto& msg : history) {
                j["history"].push_back({{"role", msg.role}, {"text", msg.text}});
            }
        }
        std::ofstream ofs(sdir / currentSessionFile);
        ofs << j.dump(2);
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
