#include "response_parser.h"
#include <algorithm>

namespace navi {

// ============================================================
//  sanitize_json_string — Strip markdown fences and extract JSON
//
//  Many LLMs return responses like:
//
//    Here is my analysis:
//    ```json
//    { "current_status": "combat", ... }
//    ```
//    Hope this helps!
//
//  We need to reliably extract { ... } from this mess.
// ============================================================
static std::string sanitize_json_string(const std::string& raw) {
    std::string s = raw;

    // Step 1: Remove ```json and ``` markdown fences.
    //         Also handle ```JSON, ``` json, etc.
    //         We do this by scanning for triple-backtick lines.
    {
        std::string cleaned;
        cleaned.reserve(s.size());

        size_t i = 0;
        while (i < s.size()) {
            // Check for ``` at current position
            if (i + 2 < s.size() && s[i] == '`' && s[i+1] == '`' && s[i+2] == '`') {
                // Skip the entire ``` ... line (until newline or end)
                i += 3;
                while (i < s.size() && s[i] != '\n' && s[i] != '\r') {
                    i++;
                }
                // Skip the newline itself
                if (i < s.size() && s[i] == '\r') i++;
                if (i < s.size() && s[i] == '\n') i++;
            } else {
                cleaned.push_back(s[i]);
                i++;
            }
        }
        s = std::move(cleaned);
    }

    // Step 2: Find the first '{' and matching last '}' to extract
    //         the JSON object.  This handles cases where the model
    //         adds explanatory text before or after the JSON.
    auto first_brace = s.find('{');
    auto last_brace  = s.rfind('}');

    if (first_brace == std::string::npos || last_brace == std::string::npos ||
        last_brace <= first_brace) {
        // No valid JSON object found — return empty so the parser
        // will hit the catch block and return fallback.
        return {};
    }

    return s.substr(first_brace, last_brace - first_brace + 1);
}

// ============================================================
//  parse_ai_response — Public API
// ============================================================
GameStateData parse_ai_response(const std::string& raw_response) {
    GameStateData result;
    result.raw_json = raw_response;

    // Guard: empty input goes straight to fallback
    if (raw_response.empty()) {
        result.is_fallback = true;
        result.tactical_advice = "Empty response from AI model.";
        return result;
    }

    try {
        // Sanitize: strip markdown fences, extract JSON block
        std::string clean_json = sanitize_json_string(raw_response);

        if (clean_json.empty()) {
            result.is_fallback = true;
            result.tactical_advice = "No JSON object found in AI response.";
            return result;
        }

        // Parse via nlohmann/json (may throw json::parse_error)
        auto j = nlohmann::json::parse(clean_json);

        // Deserialize into struct (uses our from_json with safe defaults)
        result = j.get<GameStateData>();
        result.raw_json = raw_response;
        result.is_fallback = false;

    } catch (const nlohmann::json::parse_error& e) {
        // Malformed JSON — return fallback with diagnostic info
        result.is_fallback = true;
        result.current_status = "unknown";
        result.tactical_advice = std::string("JSON parse error: ") + e.what();

    } catch (const nlohmann::json::type_error& e) {
        // Type mismatch (e.g. expected string but got number)
        result.is_fallback = true;
        result.current_status = "unknown";
        result.tactical_advice = std::string("JSON type error: ") + e.what();

    } catch (const std::exception& e) {
        // Catch-all for any unforeseen exception
        result.is_fallback = true;
        result.current_status = "unknown";
        result.tactical_advice = std::string("Unexpected error: ") + e.what();
    }

    return result;
}

} // namespace navi
