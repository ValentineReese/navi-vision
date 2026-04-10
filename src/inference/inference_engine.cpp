#include "inference_engine.h"
#include <sstream>

namespace navi {

// ============================================================
//  MockInference::analyze_frame
//
//  如果设置了 GameProfile，则使用配置中的 mock_scenarios；
//  否则退回内置的默认场景。
// ============================================================
std::string MockInference::analyze_frame(
    const std::vector<uint8_t>& image_data,
    int width, int height,
    const std::string& voice_text_prompt) {

    (void)image_data;

    call_count_++;

    // ── 如果配置了 GameProfile 且有 mock 场景，从中轮换 ──
    if (profile_ && !profile_->mock_scenarios.empty()) {
        int idx = call_count_ % static_cast<int>(profile_->mock_scenarios.size());
        const auto& sc = profile_->mock_scenarios[idx];

        // 构建 JSON
        nlohmann::json j;
        j["current_status"]  = sc.current_status;
        j["detected_units"]  = sc.detected_units;
        j["confidence"]      = sc.confidence;

        if (!voice_text_prompt.empty()) {
            j["tactical_advice"] = "You asked: " + voice_text_prompt +
                                   " — " + sc.tactical_advice;
        } else {
            j["tactical_advice"] = sc.tactical_advice;
        }

        std::string json_str = j.dump(4);

        // 如果场景标记了 wrap_markdown，用 ```json 包裹（测试 sanitizer）
        if (sc.wrap_markdown) {
            std::ostringstream oss;
            oss << "Here is my analysis (" << width << "x" << height << "):\n";
            oss << "```json\n" << json_str << "\n```\n";
            return oss.str();
        }
        return json_str;
    }

    // ── 无配置时：使用内置默认场景 ──
    int scenario = call_count_ % 4;

    std::ostringstream oss;

    switch (scenario) {
    case 0:
        oss << R"({
    "current_status": "safe",
    "detected_units": ["Peasant", "Footman"],
    "tactical_advice": "Economy is stable. Consider teching to tier 2.",
    "confidence": 0.92
})";
        break;

    case 1:
        oss << R"({
    "current_status": "combat",
    "detected_units": ["Grunt", "Raider", "Blademaster", "Witch Doctor"],
    "tactical_advice": "Focus fire on Blademaster. Use Holy Light on your Archmage.",
    "confidence": 0.85
})";
        break;

    case 2:
        oss << "Here is my analysis of the frame (" << width << "x" << height << "):\n";
        oss << "```json\n";
        oss << R"({
    "current_status": "retreating",
    "detected_units": ["Tauren Chieftain", "Kodo Beast", "Spirit Walker"],
    "tactical_advice": "Outnumbered. Fall back to base and mass towers.",
    "confidence": 0.78
})";
        oss << "\n```\n";
        oss << "Let me know if you need more detail.";
        break;

    case 3:
        oss << R"({
    "current_status": "combat",
    "detected_units": ["Abomination", "Lich", "Meat Wagon"],
    "tactical_advice": ")";
        if (voice_text_prompt.empty()) {
            oss << "Undead push detected. Build Gryphon Riders for air superiority.";
        } else {
            oss << "You asked: " << voice_text_prompt
                << " — Recommend dispelling enemy buffs first.";
        }
        oss << R"(",
    "confidence": 0.88
})";
        break;
    }

    return oss.str();
}

} // namespace navi
