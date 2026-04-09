#pragma once

#include <string>
#include "game_state.h"

namespace navi {

// ============================================================
//  parse_ai_response — Robust JSON deserializer with sanitization
//
//  AI models often wrap their JSON output in markdown fences
//  like ```json ... ``` or add explanatory text before/after
//  the actual JSON object.  This function:
//
//    1. Strips markdown code fences (```json, ```)
//    2. Extracts the first valid JSON object { ... } from the text
//    3. Deserializes into GameStateData using nlohmann/json
//    4. On ANY failure, returns a safe fallback struct instead
//       of letting the C++ main program crash
//
//  The caller can check `result.is_fallback` to know whether
//  the parse succeeded or a default was returned.
// ============================================================
GameStateData parse_ai_response(const std::string& raw_response);

} // namespace navi
