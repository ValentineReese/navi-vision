#pragma once

#include <string>
#include <vector>
#include <array>
#include <map>
#include <filesystem>
#include "../third_party/nlohmann/json.hpp"

namespace navi {

// ============================================================
//  StatusStyle — 每个状态值对应的显示样式
// ============================================================
struct StatusStyle {
    std::array<float, 3> color = {0.6f, 0.6f, 0.6f};  // RGB [0,1]
    std::string label;                                   // 显示名称
};

// ============================================================
//  MockScenario — 用于 MockInference 的预设场景
// ============================================================
struct MockScenario {
    std::string current_status;
    std::vector<std::string> detected_units;
    std::string tactical_advice;
    float confidence = 0.85f;
    bool wrap_markdown = false;  // 是否用 ```json 包裹（测试 sanitizer）
};

// ============================================================
//  DisplayLabels — UI 中各字段的显示标签
// ============================================================
struct DisplayLabels {
    std::string status          = "Status";
    std::string detected_items  = "Detected Units";
    std::string advice          = "Tactical Advice";
    std::string confidence      = "Confidence";
};

// ============================================================
//  GameProfile — 游戏配置文件
//
//  从 JSON 加载，定义了:
//  - 系统提示词 (VLM 推理)
//  - 用户提示词
//  - 状态值 → 颜色/标签映射
//  - UI 显示标签
//  - Mock 测试场景
// ============================================================
struct GameProfile {
    std::string name;
    std::string version;
    std::string description;

    // VLM 推理配置
    std::string system_prompt;
    std::string user_prompt;

    // 状态 → 显示样式
    std::map<std::string, StatusStyle> status_styles;

    // UI 标签
    DisplayLabels display_labels;

    // Mock 场景
    std::vector<MockScenario> mock_scenarios;

    // 来源文件路径
    std::string source_path;

    /// 从 JSON 文件加载
    static GameProfile loadFromFile(const std::string& path);

    /// 获取状态颜色，找不到时返回灰色
    std::array<float, 3> getStatusColor(const std::string& status) const;

    /// 获取状态显示标签
    std::string getStatusLabel(const std::string& status) const;
};

// ============================================================
//  ProfileManager — 扫描 profiles 目录，管理多个游戏配置
// ============================================================
class ProfileManager {
public:
    /// 扫描指定目录下的所有 .json 文件并加载
    void scanDirectory(const std::string& dir);

    /// 获取所有已加载的配置名列表
    std::vector<std::string> getProfileNames() const;

    /// 按名称获取配置（找不到返回 nullptr）
    const GameProfile* getProfile(const std::string& name) const;

    /// 按索引获取
    const GameProfile* getProfile(int index) const;

    /// 配置数量
    int count() const { return static_cast<int>(profiles_.size()); }

    /// 获取 profiles 目录路径（exe 旁的 profiles 文件夹）
    static std::string getProfilesDir();

private:
    std::vector<GameProfile> profiles_;
};

} // namespace navi
