#include "game_profile.h"
#include "../platform/platform.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <filesystem>

namespace navi {

// ============================================================
//  GameProfile::loadFromFile
// ============================================================
GameProfile GameProfile::loadFromFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open profile: " + path);
    }

    nlohmann::json j;
    ifs >> j;

    GameProfile p;
    p.source_path    = path;
    p.name           = j.value("name", "Unnamed");
    p.version        = j.value("version", "1.0");
    p.description    = j.value("description", "");
    p.system_prompt  = j.value("system_prompt", "");
    p.user_prompt    = j.value("user_prompt", "Analyze the game screenshot and respond with JSON:");

    // 状态样式
    if (j.contains("status_styles") && j["status_styles"].is_object()) {
        for (auto& [key, val] : j["status_styles"].items()) {
            StatusStyle ss;
            if (val.contains("color") && val["color"].is_array() && val["color"].size() == 3) {
                ss.color[0] = val["color"][0].get<float>();
                ss.color[1] = val["color"][1].get<float>();
                ss.color[2] = val["color"][2].get<float>();
            }
            ss.label = val.value("label", key);
            p.status_styles[key] = ss;
        }
    }

    // 显示标签
    if (j.contains("display_labels") && j["display_labels"].is_object()) {
        auto& dl = j["display_labels"];
        p.display_labels.status         = dl.value("status", "Status");
        p.display_labels.detected_items = dl.value("detected_items", "Detected Units");
        p.display_labels.advice         = dl.value("advice", "Tactical Advice");
        p.display_labels.confidence     = dl.value("confidence", "Confidence");
    }

    // Mock 场景
    if (j.contains("mock_scenarios") && j["mock_scenarios"].is_array()) {
        for (auto& sc : j["mock_scenarios"]) {
            MockScenario ms;
            ms.current_status  = sc.value("current_status", "unknown");
            ms.tactical_advice = sc.value("tactical_advice", "");
            ms.confidence      = sc.value("confidence", 0.85f);
            ms.wrap_markdown   = sc.value("wrap_markdown", false);
            if (sc.contains("detected_units") && sc["detected_units"].is_array()) {
                ms.detected_units = sc["detected_units"].get<std::vector<std::string>>();
            }
            p.mock_scenarios.push_back(std::move(ms));
        }
    }

    return p;
}

// ============================================================
//  GameProfile — 辅助查询
// ============================================================

std::array<float, 3> GameProfile::getStatusColor(const std::string& status) const {
    auto it = status_styles.find(status);
    if (it != status_styles.end()) {
        return it->second.color;
    }
    return {0.6f, 0.6f, 0.6f};  // 默认灰色
}

std::string GameProfile::getStatusLabel(const std::string& status) const {
    auto it = status_styles.find(status);
    if (it != status_styles.end() && !it->second.label.empty()) {
        return it->second.label;
    }
    return status;
}

// ============================================================
//  ProfileManager
// ============================================================

void ProfileManager::scanDirectory(const std::string& dir) {
    profiles_.clear();

    if (!std::filesystem::exists(dir)) {
        return;
    }

    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            try {
                auto profile = GameProfile::loadFromFile(entry.path().string());
                profiles_.push_back(std::move(profile));
            } catch (const std::exception& e) {
                std::cerr << "Failed to load profile " << entry.path()
                          << ": " << e.what() << std::endl;
            }
        }
    }
}

std::vector<std::string> ProfileManager::getProfileNames() const {
    std::vector<std::string> names;
    names.reserve(profiles_.size());
    for (auto& p : profiles_) {
        names.push_back(p.name);
    }
    return names;
}

const GameProfile* ProfileManager::getProfile(const std::string& name) const {
    for (auto& p : profiles_) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

const GameProfile* ProfileManager::getProfile(int index) const {
    if (index >= 0 && index < static_cast<int>(profiles_.size())) {
        return &profiles_[index];
    }
    return nullptr;
}

std::string ProfileManager::getProfilesDir() {
    // exe 旁的 profiles 文件夹
    return (std::filesystem::path(platform::getExecutableDir()) / "profiles").string();
}

} // namespace navi
