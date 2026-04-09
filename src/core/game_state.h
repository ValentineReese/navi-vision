#pragma once

#include <string>
#include <vector>
#include "../third_party/nlohmann/json.hpp"

namespace navi {

// ============================================================
//  GameStateData — Structured data contract for AI model output
//
//  This struct strictly maps the JSON schema that the multimodal
//  AI model is expected to return after analyzing a game frame.
//  Designed for Warcraft III scenarios but extensible to other
//  RTS/MOBA titles by adding domain-specific fields.
// ============================================================
struct GameStateData {
    // Overall battlefield assessment.
    // Expected values: "safe", "combat", "retreating", "building", "unknown"
    std::string current_status = "unknown";

    // List of enemy units detected in the current frame.
    // e.g. ["Grunt", "Peon", "Blademaster", "Watch Tower"]
    std::vector<std::string> detected_units;

    // One-sentence tactical recommendation from the AI.
    // e.g. "Focus fire on the enemy hero before engaging creeps."
    std::string tactical_advice = "No advice available.";

    // Confidence score [0.0, 1.0] indicating how sure the model is.
    // A low score signals that the response may be unreliable.
    float confidence = 0.0f;

    // Raw JSON string preserved for debugging / logging purposes.
    std::string raw_json;

    // Whether this struct was populated from a successful parse
    // or fell back to defaults due to a parse error.
    bool is_fallback = true;
};

// ============================================================
//  nlohmann/json serialization support
//
//  from_json: Deserialize with safe defaults for missing fields.
//  to_json:   Serialize for logging / round-trip testing.
// ============================================================

inline void from_json(const nlohmann::json& j, GameStateData& g) {
    // Use value() with default fallbacks so missing keys never throw.
    g.current_status = j.value("current_status", "unknown");
    g.tactical_advice = j.value("tactical_advice", "No advice available.");
    g.confidence = j.value("confidence", 0.0f);

    // detected_units may be absent or not an array — handle gracefully.
    if (j.contains("detected_units") && j["detected_units"].is_array()) {
        g.detected_units = j["detected_units"].get<std::vector<std::string>>();
    }

    g.is_fallback = false;
}

inline void to_json(nlohmann::json& j, const GameStateData& g) {
    j = nlohmann::json{
        {"current_status",  g.current_status},
        {"detected_units",  g.detected_units},
        {"tactical_advice", g.tactical_advice},
        {"confidence",      g.confidence}
    };
}

} // namespace navi
