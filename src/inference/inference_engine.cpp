#include "inference_engine.h"
#include <sstream>

namespace navi {

// ============================================================
//  MockInference::analyze_frame
//
//  Rotates through several mock scenarios so testers can
//  verify that the GUI handles all possible game states.
//  In production this will be replaced by llama.cpp calls.
// ============================================================
std::string MockInference::analyze_frame(
    const std::vector<uint8_t>& image_data,
    int width, int height,
    const std::string& voice_text_prompt) {

    // Suppress unused-parameter warnings while keeping the signature
    (void)image_data;

    call_count_++;
    int scenario = call_count_ % 4;

    std::ostringstream oss;

    switch (scenario) {
    case 0:
        // Scenario A: Safe base-building phase
        oss << R"({
    "current_status": "safe",
    "detected_units": ["Peasant", "Footman"],
    "tactical_advice": "Economy is stable. Consider teching to tier 2.",
    "confidence": 0.92
})";
        break;

    case 1:
        // Scenario B: Active combat with multiple enemies
        oss << R"({
    "current_status": "combat",
    "detected_units": ["Grunt", "Raider", "Blademaster", "Witch Doctor"],
    "tactical_advice": "Focus fire on Blademaster. Use Holy Light on your Archmage.",
    "confidence": 0.85
})";
        break;

    case 2:
        // Scenario C: Response wrapped in markdown fences (tests sanitizer)
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
        // Scenario D: Include voice prompt echo for testing voice path
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
