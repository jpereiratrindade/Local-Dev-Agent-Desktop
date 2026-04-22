#include "TurnDiffTracker.hpp"

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace agent::core {
namespace {

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

TurnDiffTracker::TurnDiffTracker(std::string root) {
    try {
        workspaceRoot = fs::weakly_canonical(fs::path(root.empty() ? "." : root));
    } catch (...) {
        workspaceRoot = fs::current_path();
    }
}

fs::path TurnDiffTracker::resolvePath(const std::string& path) const {
    fs::path candidate(path);
    if (candidate.is_relative()) candidate = workspaceRoot / candidate;
    try {
        return fs::weakly_canonical(candidate);
    } catch (...) {
        return candidate.lexically_normal();
    }
}

void TurnDiffTracker::snapshotBefore(const std::string& path) {
    if (path.empty()) return;
    fs::path resolved = resolvePath(path);
    if (baselines.find(resolved) != baselines.end()) return;

    Snapshot snapshot;
    snapshot.existed = fs::exists(resolved) && fs::is_regular_file(resolved);
    if (snapshot.existed) snapshot.content = readFile(resolved);
    baselines.emplace(resolved, std::move(snapshot));
}

void TurnDiffTracker::recordMutation(const std::string& path) {
    if (path.empty()) return;
    fs::path resolved = resolvePath(path);
    touched.insert(resolved);
}

std::vector<std::string> TurnDiffTracker::changedFiles() const {
    std::vector<std::string> files;
    for (const auto& path : touched) {
        auto it = baselines.find(path);
        const bool existsNow = fs::exists(path) && fs::is_regular_file(path);
        const std::string current = existsNow ? readFile(path) : "";

        bool changed = true;
        if (it != baselines.end()) {
            changed = (it->second.existed != existsNow) || (it->second.content != current);
        }
        if (changed) files.push_back(path.string());
    }
    return files;
}

std::string TurnDiffTracker::summarize() const {
    auto files = changedFiles();
    if (files.empty()) return "TurnDiffTracker: nenhuma mudança detectada.";

    std::stringstream out;
    out << "TurnDiffTracker: " << files.size() << " arquivo(s) alterado(s):";
    for (const auto& file : files) out << "\n- " << file;
    return out.str();
}

} // namespace agent::core
