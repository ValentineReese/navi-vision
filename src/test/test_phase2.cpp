// ============================================================
//  NaviVision Unit Test — Phase 2 Verification
//
//  This is a standalone console application that validates:
//
//  1. Desktop snapshot capture (BitBlt-based, no WGC needed)
//     - Proves that BGR pixel data can be obtained and is valid
//
//  2. JSON response parser with sanitization
//     - Clean JSON, markdown-fenced JSON, malformed JSON,
//       empty input, missing fields, wrong types
//
//  3. Mock inference engine
//     - Verifies all 4 rotation scenarios produce parseable output
//
//  4. End-to-end pipeline
//     - Capture desktop → feed to MockInference → parse → validate
//
//  Build:  Part of the NaviVisionTest CMake target
//  Run:    NaviVisionTest.exe  (console output with PASS/FAIL)
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <cstdint>

#include "../core/game_state.h"
#include "../core/response_parser.h"
#include "../inference/inference_engine.h"
#include "../core/frame_buffer.h"

// ============================================================
//  Test Utilities
// ============================================================

static int g_total  = 0;
static int g_passed = 0;
static int g_failed = 0;

#define TEST_ASSERT(cond, msg)                                          \
    do {                                                                \
        g_total++;                                                      \
        if (cond) {                                                     \
            g_passed++;                                                 \
            std::cout << "  [PASS] " << (msg) << std::endl;            \
        } else {                                                        \
            g_failed++;                                                 \
            std::cout << "  [FAIL] " << (msg) << std::endl;            \
            std::cout << "         at " << __FILE__                     \
                      << ":" << __LINE__ << std::endl;                  \
        }                                                               \
    } while (0)

// ============================================================
//  Desktop Snapshot via BitBlt
//
//  Uses GDI BitBlt to capture the entire desktop as a test
//  data source.  This is independent of WGC and works on all
//  Windows versions, making it ideal for unit testing.
// ============================================================
struct DesktopSnapshot {
    std::vector<uint8_t> pixels;  // BGR format, 3 bytes per pixel
    int width  = 0;
    int height = 0;
};

static DesktopSnapshot captureDesktop() {
    DesktopSnapshot snap;

    // Get desktop device context
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem    = CreateCompatibleDC(hdcScreen);

    snap.width  = GetSystemMetrics(SM_CXSCREEN);
    snap.height = GetSystemMetrics(SM_CYSCREEN);

    // Create a compatible bitmap for the screen
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, snap.width, snap.height);
    HGDIOBJ hOld    = SelectObject(hdcMem, hBitmap);

    // BitBlt copies the screen to our memory DC
    BitBlt(hdcMem, 0, 0, snap.width, snap.height, hdcScreen, 0, 0, SRCCOPY);

    // Extract pixel data via GetDIBits
    BITMAPINFOHEADER bi = {};
    bi.biSize        = sizeof(bi);
    bi.biWidth       = snap.width;
    bi.biHeight      = -snap.height;  // Negative = top-down row order
    bi.biPlanes      = 1;
    bi.biBitCount    = 24;            // 24-bit BGR
    bi.biCompression = BI_RGB;

    size_t rowStride = ((snap.width * 3 + 3) & ~3);  // DWORD-aligned
    snap.pixels.resize(rowStride * snap.height);

    GetDIBits(hdcMem, hBitmap, 0, snap.height,
              snap.pixels.data(),
              reinterpret_cast<BITMAPINFO*>(&bi),
              DIB_RGB_COLORS);

    // Convert from DWORD-aligned rows to tightly-packed BGR
    if (rowStride != static_cast<size_t>(snap.width * 3)) {
        std::vector<uint8_t> packed(static_cast<size_t>(snap.width) * snap.height * 3);
        for (int y = 0; y < snap.height; y++) {
            memcpy(packed.data() + y * snap.width * 3,
                   snap.pixels.data() + y * rowStride,
                   snap.width * 3);
        }
        snap.pixels = std::move(packed);
    }

    // Cleanup GDI resources
    SelectObject(hdcMem, hOld);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    return snap;
}

// ============================================================
//  Test Group 1: Desktop Snapshot Validity
// ============================================================
static void testDesktopCapture() {
    std::cout << "\n=== Test Group 1: Desktop Snapshot ===" << std::endl;

    auto snap = captureDesktop();

    TEST_ASSERT(snap.width > 0 && snap.height > 0,
                "Desktop resolution is non-zero");

    size_t expected = static_cast<size_t>(snap.width) * snap.height * 3;
    TEST_ASSERT(snap.pixels.size() == expected,
                "Pixel buffer size matches width * height * 3");

    // Make sure the image is not all-black (at least some pixels have value)
    bool hasNonZero = false;
    for (size_t i = 0; i < snap.pixels.size() && !hasNonZero; i++) {
        if (snap.pixels[i] > 0) hasNonZero = true;
    }
    TEST_ASSERT(hasNonZero,
                "Screenshot is not all-black (visual data exists)");

    std::cout << "  Desktop: " << snap.width << "x" << snap.height
              << " (" << snap.pixels.size() << " bytes)" << std::endl;
}

// ============================================================
//  Test Group 2: JSON Parser — Clean JSON
// ============================================================
static void testParserCleanJson() {
    std::cout << "\n=== Test Group 2: Parser — Clean JSON ===" << std::endl;

    std::string input = R"({
        "current_status": "combat",
        "detected_units": ["Grunt", "Raider"],
        "tactical_advice": "Focus fire on Grunt.",
        "confidence": 0.9
    })";

    auto result = navi::parse_ai_response(input);

    TEST_ASSERT(!result.is_fallback,
                "Clean JSON parses successfully (not fallback)");
    TEST_ASSERT(result.current_status == "combat",
                "current_status == 'combat'");
    TEST_ASSERT(result.detected_units.size() == 2,
                "detected_units has 2 entries");
    TEST_ASSERT(result.detected_units[0] == "Grunt",
                "First unit is 'Grunt'");
    TEST_ASSERT(result.tactical_advice == "Focus fire on Grunt.",
                "tactical_advice matches");
    TEST_ASSERT(result.confidence > 0.89f && result.confidence < 0.91f,
                "confidence is ~0.9");
}

// ============================================================
//  Test Group 3: JSON Parser — Markdown-fenced JSON
// ============================================================
static void testParserMarkdownFenced() {
    std::cout << "\n=== Test Group 3: Parser — Markdown Fenced ===" << std::endl;

    std::string input =
        "Here is the analysis:\n"
        "```json\n"
        "{\n"
        "  \"current_status\": \"safe\",\n"
        "  \"detected_units\": [\"Peasant\"],\n"
        "  \"tactical_advice\": \"Build more farms.\",\n"
        "  \"confidence\": 0.95\n"
        "}\n"
        "```\n"
        "Hope this helps!";

    auto result = navi::parse_ai_response(input);

    TEST_ASSERT(!result.is_fallback,
                "Markdown-fenced JSON parses successfully");
    TEST_ASSERT(result.current_status == "safe",
                "current_status == 'safe'");
    TEST_ASSERT(result.detected_units.size() == 1,
                "detected_units has 1 entry");
    TEST_ASSERT(result.confidence > 0.94f,
                "confidence is ~0.95");
}

// ============================================================
//  Test Group 4: JSON Parser — Malformed / Edge Cases
// ============================================================
static void testParserEdgeCases() {
    std::cout << "\n=== Test Group 4: Parser — Edge Cases ===" << std::endl;

    // 4a: Completely broken JSON
    {
        auto r = navi::parse_ai_response("{broken json!!!");
        TEST_ASSERT(r.is_fallback, "Broken JSON returns fallback");
        TEST_ASSERT(r.current_status == "unknown",
                    "Fallback status is 'unknown'");
    }

    // 4b: Empty input
    {
        auto r = navi::parse_ai_response("");
        TEST_ASSERT(r.is_fallback, "Empty input returns fallback");
    }

    // 4c: No JSON at all, just text
    {
        auto r = navi::parse_ai_response("I cannot analyze this image.");
        TEST_ASSERT(r.is_fallback, "Plain text returns fallback");
    }

    // 4d: Valid JSON but missing fields — should use defaults
    {
        auto r = navi::parse_ai_response(R"({"current_status": "building"})");
        TEST_ASSERT(!r.is_fallback,
                    "Partial JSON does NOT fallback");
        TEST_ASSERT(r.current_status == "building",
                    "Parsed field is correct");
        TEST_ASSERT(r.detected_units.empty(),
                    "Missing detected_units defaults to empty");
        TEST_ASSERT(r.confidence < 0.01f,
                    "Missing confidence defaults to 0.0");
    }

    // 4e: Nested markdown with extra text
    {
        std::string input =
            "Sure! ```json\n{\"current_status\":\"combat\",\"detected_units\":[],\"tactical_advice\":\"Run!\",\"confidence\":0.5}\n``` end.";
        auto r = navi::parse_ai_response(input);
        TEST_ASSERT(!r.is_fallback, "Inline markdown fence parses OK");
        TEST_ASSERT(r.tactical_advice == "Run!",
                    "Tactical advice from inline fenced JSON");
    }
}

// ============================================================
//  Test Group 5: Mock Inference Engine
// ============================================================
static void testMockInference() {
    std::cout << "\n=== Test Group 5: Mock Inference ===" << std::endl;

    navi::MockInference mock;

    TEST_ASSERT(mock.engine_name() == "MockInference v1.0",
                "Engine name is correct");

    // Dummy image data (small 2x2 BGR)
    std::vector<uint8_t> img(2 * 2 * 3, 128);

    // Call 4 times to exercise all rotation scenarios
    for (int i = 0; i < 4; i++) {
        std::string raw = mock.analyze_frame(img, 2, 2, "");
        auto result = navi::parse_ai_response(raw);

        std::string label = "Scenario " + std::to_string(i) + " parses successfully";
        TEST_ASSERT(!result.is_fallback, label.c_str());

        label = "Scenario " + std::to_string(i) + " has valid status";
        TEST_ASSERT(!result.current_status.empty() &&
                    result.current_status != "unknown",
                    label.c_str());
    }
}

// ============================================================
//  Test Group 6: FrameBuffer Thread Safety (basic)
// ============================================================
static void testFrameBuffer() {
    std::cout << "\n=== Test Group 6: FrameBuffer ===" << std::endl;

    navi::FrameBuffer buf;

    // Initially empty
    TEST_ASSERT(buf.read() == nullptr, "Empty buffer returns nullptr");

    // Write a frame
    auto frame = std::make_shared<navi::FrameData>();
    frame->width  = 100;
    frame->height = 200;
    frame->pixels.resize(100 * 200 * 4, 42);
    buf.write(frame);

    auto read = buf.read();
    TEST_ASSERT(read != nullptr, "After write, read is non-null");
    TEST_ASSERT(read->width == 100 && read->height == 200,
                "Read frame dimensions match");

    // Clear
    buf.clear();
    TEST_ASSERT(buf.read() == nullptr, "After clear, buffer is empty");
}

// ============================================================
//  Test Group 7: End-to-End Pipeline
//
//  Desktop capture → MockInference → parse_ai_response → validate
// ============================================================
static void testEndToEnd() {
    std::cout << "\n=== Test Group 7: End-to-End Pipeline ===" << std::endl;

    auto snap = captureDesktop();
    TEST_ASSERT(!snap.pixels.empty(), "Desktop capture produced data");

    navi::MockInference mock;
    std::string raw = mock.analyze_frame(
        snap.pixels, snap.width, snap.height, "What units do you see?");

    auto result = navi::parse_ai_response(raw);
    TEST_ASSERT(!result.is_fallback,
                "E2E: AI response parsed without fallback");
    TEST_ASSERT(!result.tactical_advice.empty(),
                "E2E: Tactical advice is non-empty");
    TEST_ASSERT(!result.current_status.empty(),
                "E2E: Status is non-empty");

    std::cout << "  E2E result:"
              << " status=" << result.current_status
              << " units=" << result.detected_units.size()
              << " confidence=" << result.confidence << std::endl;
}

// ============================================================
//  Main
// ============================================================
int main() {
    std::cout << "================================================" << std::endl;
    std::cout << "  NaviVision Unit Tests — Phase 2 Verification  " << std::endl;
    std::cout << "================================================" << std::endl;

    testDesktopCapture();
    testParserCleanJson();
    testParserMarkdownFenced();
    testParserEdgeCases();
    testMockInference();
    testFrameBuffer();
    testEndToEnd();

    std::cout << "\n================================================" << std::endl;
    std::cout << "  Results: " << g_passed << " passed, "
              << g_failed << " failed, "
              << g_total << " total" << std::endl;
    std::cout << "================================================" << std::endl;

    return (g_failed == 0) ? 0 : 1;
}
