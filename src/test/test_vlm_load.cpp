// ============================================================
//  NaviVision VLM Load Test
//
//  Attempts to load Qwen2.5-VL model files to diagnose loading
//  failures. Reports detailed error messages from llama.cpp.
//
//  Build:  Part of the VlmLoadTest CMake target
//  Run:    VlmLoadTest.exe  (console output)
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
#include <filesystem>

// llama.cpp + mtmd headers
#include <llama.h>
#include <mtmd.h>
#include <mtmd-helper.h>

#include "../models/model_manager.h"
#include "../inference/vlm_engine.h"

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
//  Test 1: Verify model files exist and have reasonable size
// ============================================================
static void testModelFilesExist() {
    std::cout << "\n=== Test 1: Model Files Existence ===" << std::endl;

    auto models = navi::ModelManager::getDefaultModels();
    auto modelsDir = navi::ModelManager::getModelsDir();
    std::cout << "  Models dir: " << modelsDir << std::endl;

    // Find the Q4_K_M 3B entry
    const navi::ModelEntry* entry = nullptr;
    for (const auto& m : models) {
        if (m.name.find("3B") != std::string::npos &&
            m.name.find("Q4_K_M") != std::string::npos) {
            entry = &m;
            break;
        }
    }

    TEST_ASSERT(entry != nullptr, "Found 3B Q4_K_M preset entry");
    if (!entry) return;

    std::string modelPath  = navi::ModelManager::getModelPath(*entry);
    std::string mmprojPath = navi::ModelManager::getMmprojPath(*entry);

    std::cout << "  Model path:  " << modelPath << std::endl;
    std::cout << "  Mmproj path: " << mmprojPath << std::endl;

    bool modelExists  = std::filesystem::exists(modelPath);
    bool mmprojExists = std::filesystem::exists(mmprojPath);

    TEST_ASSERT(modelExists, "Model file exists");
    TEST_ASSERT(mmprojExists, "Mmproj file exists");

    if (modelExists) {
        auto sz = std::filesystem::file_size(modelPath);
        std::cout << "  Model size:  " << navi::ModelManager::formatBytes(sz) << std::endl;
        TEST_ASSERT(sz > 1000000000ULL, "Model file > 1 GB (reasonable for Q4_K_M)");
    }
    if (mmprojExists) {
        auto sz = std::filesystem::file_size(mmprojPath);
        std::cout << "  Mmproj size: " << navi::ModelManager::formatBytes(sz) << std::endl;
        TEST_ASSERT(sz > 100000000ULL, "Mmproj file > 100 MB (reasonable for f16 projector)");
    }
}

// ============================================================
//  Test 2: Low-level llama.cpp model load
// ============================================================
static void testLlamaModelLoad() {
    std::cout << "\n=== Test 2: llama_load_model_from_file ===" << std::endl;

    auto models = navi::ModelManager::getDefaultModels();
    const navi::ModelEntry* entry = nullptr;
    for (const auto& m : models) {
        if (m.name.find("3B") != std::string::npos &&
            m.name.find("Q4_K_M") != std::string::npos) {
            entry = &m;
            break;
        }
    }
    if (!entry) {
        std::cout << "  [SKIP] No 3B Q4_K_M entry found" << std::endl;
        return;
    }

    std::string modelPath = navi::ModelManager::getModelPath(*entry);
    if (!std::filesystem::exists(modelPath)) {
        std::cout << "  [SKIP] Model file not found: " << modelPath << std::endl;
        return;
    }

    std::cout << "  Initializing llama backend..." << std::endl;
    llama_backend_init();

    std::cout << "  Loading model: " << modelPath << std::endl;
    llama_model_params params = llama_model_default_params();
    params.n_gpu_layers = 0;   // CPU only for test (avoid VRAM issues)
    params.use_mmap     = true;

    llama_model* model = llama_model_load_from_file(modelPath.c_str(), params);
    TEST_ASSERT(model != nullptr, "llama_model_load_from_file succeeded");

    if (model) {
        const llama_vocab* vocab = llama_model_get_vocab(model);
        int n_vocab = llama_vocab_n_tokens(vocab);
        std::cout << "  n_vocab = " << n_vocab << std::endl;
        TEST_ASSERT(n_vocab > 0, "Model has valid vocabulary");

        // Try creating a context
        std::cout << "  Creating context..." << std::endl;
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx     = 2048;
        ctx_params.n_threads = 2;
        ctx_params.n_batch   = 512;

        llama_context* ctx = llama_new_context_with_model(model, ctx_params);
        TEST_ASSERT(ctx != nullptr, "llama_new_context_with_model succeeded");

        if (ctx) {
            llama_free(ctx);
            std::cout << "  Context freed OK" << std::endl;
        }

        llama_model_free(model);
        std::cout << "  Model freed OK" << std::endl;
    } else {
        std::cout << "  ** Model load FAILED. llama.cpp stderr output above "
                  << "should indicate the cause." << std::endl;
        std::cout << "  ** Common causes:" << std::endl;
        std::cout << "     - Architecture not supported (qwen2vl needs newer llama.cpp)" << std::endl;
        std::cout << "     - Corrupted or truncated GGUF file" << std::endl;
        std::cout << "     - GGUF version mismatch" << std::endl;
    }
}

// ============================================================
//  Test 3: mtmd context load (mmproj via new mtmd API)
// ============================================================
static void testMtmdContextLoad() {
    std::cout << "\n=== Test 3: mtmd_init_from_file ===" << std::endl;

    auto models = navi::ModelManager::getDefaultModels();
    const navi::ModelEntry* entry = nullptr;
    for (const auto& m : models) {
        if (m.name.find("3B") != std::string::npos &&
            m.name.find("Q4_K_M") != std::string::npos) {
            entry = &m;
            break;
        }
    }
    if (!entry) {
        std::cout << "  [SKIP] No 3B Q4_K_M entry found" << std::endl;
        return;
    }

    std::string modelPath  = navi::ModelManager::getModelPath(*entry);
    std::string mmprojPath = navi::ModelManager::getMmprojPath(*entry);
    if (!std::filesystem::exists(modelPath) ||
        !std::filesystem::exists(mmprojPath)) {
        std::cout << "  [SKIP] Model files not found" << std::endl;
        return;
    }

    // Need the main model loaded first for mtmd_init_from_file
    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 0;
    mparams.use_mmap     = true;
    llama_model* model = llama_model_load_from_file(modelPath.c_str(), mparams);
    if (!model) {
        std::cout << "  [SKIP] Main model failed to load" << std::endl;
        return;
    }

    std::cout << "  Loading mtmd context: " << mmprojPath << std::endl;
    mtmd_context_params ctx_params = mtmd_context_params_default();
    ctx_params.use_gpu       = false;
    ctx_params.print_timings = false;
    ctx_params.n_threads     = 2;

    mtmd_context* mtmdCtx = mtmd_init_from_file(mmprojPath.c_str(), model, ctx_params);
    TEST_ASSERT(mtmdCtx != nullptr, "mtmd_init_from_file succeeded");

    if (mtmdCtx) {
        bool supportsVision = mtmd_support_vision(mtmdCtx);
        std::cout << "  Supports vision: " << (supportsVision ? "yes" : "no") << std::endl;
        TEST_ASSERT(supportsVision, "Model supports vision input");

        mtmd_free(mtmdCtx);
        std::cout << "  mtmd context freed OK" << std::endl;
    } else {
        std::cout << "  ** mtmd_init_from_file FAILED. Check stderr output above." << std::endl;
    }

    llama_model_free(model);
}

// ============================================================
//  Test 4: Full VlmEngine load via our wrapper
// ============================================================
static void testVlmEngineLoad() {
    std::cout << "\n=== Test 4: VlmEngine Full Load ===" << std::endl;

    auto models = navi::ModelManager::getDefaultModels();
    const navi::ModelEntry* entry = nullptr;
    for (const auto& m : models) {
        if (m.name.find("3B") != std::string::npos &&
            m.name.find("Q4_K_M") != std::string::npos) {
            entry = &m;
            break;
        }
    }
    if (!entry) {
        std::cout << "  [SKIP] No 3B Q4_K_M entry found" << std::endl;
        return;
    }

    std::string modelPath  = navi::ModelManager::getModelPath(*entry);
    std::string mmprojPath = navi::ModelManager::getMmprojPath(*entry);

    if (!std::filesystem::exists(modelPath) ||
        !std::filesystem::exists(mmprojPath)) {
        std::cout << "  [SKIP] Model files missing" << std::endl;
        return;
    }

    navi::VlmConfig cfg;
    cfg.model_path  = modelPath;
    cfg.mmproj_path = mmprojPath;
    cfg.n_gpu_layers = 0;  // CPU only for test
    cfg.n_threads    = 2;
    cfg.n_ctx        = 2048;
    cfg.n_batch      = 512;

    std::cout << "  Creating VlmEngine (CPU mode)..." << std::endl;
    auto engine = std::make_unique<navi::VlmEngine>(cfg);

    TEST_ASSERT(engine->is_loaded(), "VlmEngine loaded successfully");

    if (!engine->is_loaded()) {
        std::cout << "  ** VlmEngine load FAILED!" << std::endl;
        std::cout << "  ** Error: " << engine->last_error() << std::endl;
    } else {
        std::cout << "  Engine name: " << engine->engine_name() << std::endl;
    }
}

// ============================================================
//  Main
// ============================================================
int main() {
    std::cout << "================================================" << std::endl;
    std::cout << "  NaviVision VLM Load Test                      " << std::endl;
    std::cout << "================================================" << std::endl;

    testModelFilesExist();
    testLlamaModelLoad();
    testMtmdContextLoad();
    testVlmEngineLoad();

    std::cout << "\n================================================" << std::endl;
    std::cout << "  Results: " << g_passed << " passed, "
              << g_failed << " failed, "
              << g_total << " total" << std::endl;
    std::cout << "================================================" << std::endl;

    return (g_failed == 0) ? 0 : 1;
}
