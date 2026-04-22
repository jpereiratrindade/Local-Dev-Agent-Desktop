#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace agent::core {

class TurnDiffTracker {
public:
    explicit TurnDiffTracker(std::string workspaceRoot = ".");

    void snapshotBefore(const std::string& path);
    void recordMutation(const std::string& path);
    std::vector<std::string> changedFiles() const;
    std::string summarize() const;

private:
    struct Snapshot {
        bool existed = false;
        std::string content;
    };

    std::filesystem::path workspaceRoot;
    std::map<std::filesystem::path, Snapshot> baselines;
    std::set<std::filesystem::path> touched;

    std::filesystem::path resolvePath(const std::string& path) const;
};

} // namespace agent::core
