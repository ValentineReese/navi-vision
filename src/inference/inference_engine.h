#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace navi {

// ============================================================
//  IInferenceEngine — Abstract interface for multimodal AI
//
//  This interface decouples the capture/GUI layer from the
//  actual AI backend.  In Phase 2 we use MockInference; in
//  Phase 3 we'll swap in a LlamaCppInference that calls
//  llama.cpp for real multimodal vision-language inference.
//
//  Design rationale:
//  - Virtual interface allows hot-swapping backends at runtime
//  - image_data is raw BGR bytes (matches FrameData::pixels)
//  - voice_text_prompt carries optional user voice input
//  - Returns raw JSON string; caller uses parse_ai_response()
// ============================================================
class IInferenceEngine {
public:
    virtual ~IInferenceEngine() = default;

    // Analyze a captured game frame, optionally with voice input.
    // Returns a JSON string conforming to the GameStateData schema.
    virtual std::string analyze_frame(
        const std::vector<uint8_t>& image_data,
        int width, int height,
        const std::string& voice_text_prompt) = 0;

    // Human-readable name for the engine (for UI display)
    virtual std::string engine_name() const = 0;
};

// ============================================================
//  MockInference — Hardcoded mock for frontend testing
//
//  Returns a fixed JSON string so the UI/parser pipeline can
//  be validated end-to-end without a real model loaded.
//  The response rotates between a few scenarios to exercise
//  different code paths in the parser and renderer.
// ============================================================
class MockInference : public IInferenceEngine {
public:
    std::string analyze_frame(
        const std::vector<uint8_t>& image_data,
        int width, int height,
        const std::string& voice_text_prompt) override;

    std::string engine_name() const override { return "MockInference v1.0"; }

private:
    int call_count_ = 0;
};

} // namespace navi
