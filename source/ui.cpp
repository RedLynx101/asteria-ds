#include "ui.hpp"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace asteria {
namespace {
constexpr float kImageCardX = 6.0f;
constexpr float kImageCardY = 42.0f;
constexpr float kImageCardW = 240.0f;
constexpr float kImageCardH = 192.0f;
constexpr float kPreviewFrameX = 12.0f;
constexpr float kPreviewFrameY = 62.0f;
constexpr float kPreviewFrameW = 224.0f;
constexpr float kPreviewFrameH = 148.0f;
constexpr float kPreviewDrawX = 14.0f;
constexpr float kPreviewDrawY = 64.0f;
constexpr int kPreviewTextureWidth = 256;
constexpr int kPreviewTextureHeight = 256;
constexpr std::size_t kPreviewTextureBytes = static_cast<std::size_t>(kPreviewTextureWidth * kPreviewTextureHeight * 2);

std::string batteryLabel(int value) {
    if (value < 0) return "n/a";
    value = std::clamp(value, 0, 100);
    return std::to_string(value) + "%";
}

std::string ellipsize(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) return text;
    if (limit <= 3) return text.substr(0, limit);
    return text.substr(0, limit - 3) + "...";
}

std::string bridgeLabel(const AppState& state) {
    return state.bridgeReachable ? "Bridge live" : "Bridge down";
}

std::string robotLabel(const AppState& state) {
    return state.status.connected ? "Robot online" : "Robot offline";
}

std::string leaseSummary(const AppState& state) {
    if (!state.status.leaseActive) return "Unclaimed";
    if (state.status.leaseHolderId == state.config.holderId) {
        return "Held " + std::to_string(state.status.leaseSecondsRemaining) + "s";
    }
    return ellipsize(state.status.leaseHolder, 14);
}

std::uint16_t rgb565FromMobilePreview(std::uint16_t pixel) {
    // The mobile preview file is packed as BGR565 for the 3DS bridge.
    return static_cast<std::uint16_t>(((pixel & 0x001F) << 11) | (pixel & 0x07E0) | ((pixel & 0xF800) >> 11));
}

}  // namespace

bool UiRenderer::init() {
    if (initialized_) return true;

    lastError_.clear();
    gfxInitDefault();
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        lastError_ = "C3D_Init failed";
        gfxExit();
        return false;
    }
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        lastError_ = "C2D_Init failed";
        C3D_Fini();
        gfxExit();
        return false;
    }
    C2D_Prepare();

    topTarget_ = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottomTarget_ = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    textBuf_ = C2D_TextBufNew(4096);
    if (!topTarget_ || !bottomTarget_ || !textBuf_) {
        lastError_ = "Failed to initialize render targets";
        if (textBuf_) {
            C2D_TextBufDelete(textBuf_);
            textBuf_ = nullptr;
        }
        topTarget_ = nullptr;
        bottomTarget_ = nullptr;
        C2D_Fini();
        C3D_Fini();
        gfxExit();
        return false;
    }

    previewImage_.tex = &previewTexture_;
    previewImage_.subtex = &previewSubTexture_;
    initialized_ = true;
    return true;
}

void UiRenderer::shutdown() {
    clearPreviewTexture();
    if (textBuf_) {
        C2D_TextBufDelete(textBuf_);
        textBuf_ = nullptr;
    }
    if (initialized_) {
        C2D_Fini();
        C3D_Fini();
        gfxExit();
    }
    topTarget_ = nullptr;
    bottomTarget_ = nullptr;
    initialized_ = false;
}

void UiRenderer::beginFrame() {
    if (!initialized_) return;
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TextBufClear(textBuf_);
    frameState_ = nullptr;
}

void UiRenderer::endFrame() {
    if (!initialized_) return;
    C3D_FrameEnd(0);
}

bool UiRenderer::syncPreviewTexture(const ImagePreview& preview) {
    if (!preview.ready()) {
        previewTextureReady_ = false;
        previewTextureSourceKey_.clear();
        return false;
    }
    if (previewTextureReady_ && previewTextureSourceKey_ == preview.sourceKey) {
        return true;
    }

    if (!previewTexture_.data) {
        if (!C3D_TexInitVRAM(&previewTexture_, kPreviewTextureWidth, kPreviewTextureHeight, GPU_RGB565) &&
            !C3D_TexInit(&previewTexture_, kPreviewTextureWidth, kPreviewTextureHeight, GPU_RGB565)) {
            lastError_ = "Preview texture init failed";
            previewTextureReady_ = false;
            return false;
        }
        C3D_TexSetFilter(&previewTexture_, GPU_LINEAR, GPU_LINEAR);
        C3D_TexSetWrap(&previewTexture_, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        previewImage_.tex = &previewTexture_;
        previewImage_.subtex = &previewSubTexture_;
    }

    auto* linearPixels = static_cast<std::uint16_t*>(linearAlloc(kPreviewTextureBytes));
    if (linearPixels == nullptr) {
        lastError_ = "Preview upload buffer alloc failed";
        previewTextureReady_ = false;
        return false;
    }

    std::memset(linearPixels, 0, kPreviewTextureBytes);
    const auto* sourcePixels = reinterpret_cast<const std::uint16_t*>(preview.rgb565.data());
    const int copyWidth = std::min(preview.width, kPreviewTextureWidth);
    const int copyHeight = std::min(preview.height, kPreviewTextureHeight);
    for (int row = 0; row < copyHeight; ++row) {
        for (int col = 0; col < copyWidth; ++col) {
            linearPixels[row * kPreviewTextureWidth + col] =
                rgb565FromMobilePreview(sourcePixels[row * preview.width + col]);
        }
    }

    GSPGPU_FlushDataCache(linearPixels, static_cast<u32>(kPreviewTextureBytes));
    C3D_SyncDisplayTransfer(
        reinterpret_cast<u32*>(linearPixels),
        GX_BUFFER_DIM(kPreviewTextureWidth, kPreviewTextureHeight),
        reinterpret_cast<u32*>(previewTexture_.data),
        GX_BUFFER_DIM(kPreviewTextureWidth, kPreviewTextureHeight),
        GX_TRANSFER_FLIP_VERT(0) |
            GX_TRANSFER_OUT_TILED(1) |
            GX_TRANSFER_RAW_COPY(0) |
            GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |
            GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
            GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    linearFree(linearPixels);
    C3D_TexFlush(&previewTexture_);

    previewSubTexture_.width = static_cast<u16>(copyWidth);
    previewSubTexture_.height = static_cast<u16>(copyHeight);
    previewSubTexture_.left = 0.0f;
    previewSubTexture_.right = static_cast<float>(copyWidth) / static_cast<float>(kPreviewTextureWidth);
    previewSubTexture_.top = 1.0f;
    previewSubTexture_.bottom = 1.0f - (static_cast<float>(copyHeight) / static_cast<float>(kPreviewTextureHeight));
    previewTextureReady_ = true;
    previewTextureSourceKey_ = preview.sourceKey;
    return true;
}

void UiRenderer::clearPreviewTexture() {
    if (previewTexture_.data) {
        C3D_TexDelete(&previewTexture_);
    }
    previewTexture_ = {};
    previewSubTexture_ = {};
    previewImage_ = {};
    previewImage_.tex = &previewTexture_;
    previewImage_.subtex = &previewSubTexture_;
    previewTextureReady_ = false;
    previewTextureSourceKey_.clear();
}

void UiRenderer::drawCard(float x, float y, float w, float h, u32 fill) {
    C2D_DrawRectSolid(x, y, 0.5f, w, h, fill);
    C2D_DrawRectSolid(x, y, 0.45f, w, 1.5f, colors_.stroke);
    C2D_DrawRectSolid(x, y + h - 1.5f, 0.45f, w, 1.5f, colors_.stroke);
    C2D_DrawRectSolid(x, y, 0.45f, 1.5f, h, colors_.stroke);
    C2D_DrawRectSolid(x + w - 1.5f, y, 0.45f, 1.5f, h, colors_.stroke);
}

void UiRenderer::drawText(float x, float y, float scale, u32 color, const char* fmt, ...) {
    char buffer[512];
    std::va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    C2D_Text text;
    C2D_TextParse(&text, textBuf_, buffer);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor, x, y, 0.8f, scale, scale, color);
}

void UiRenderer::drawButton(const ButtonRect& button, bool active, bool danger) {
    const u32 fill = danger ? colors_.dangerSoft : (active ? colors_.accentSoft : colors_.panelAlt);
    drawCard(button.x, button.y, button.w, button.h, fill);
    const float textY = button.y + button.h * 0.5f - 7.0f;
    drawText(button.x + 10, textY, 0.46f, danger ? colors_.danger : (active ? colors_.accent : colors_.text), "%s", button.label.c_str());
}

void UiRenderer::drawTopChat(const AppState& state) {
    drawCard(6, 4, 388, 42, colors_.panel);
    drawText(14, 8, 0.52f, colors_.accent, "Asteria DS");
    const u32 bridgeColor = state.bridgeReachable ? colors_.success : colors_.danger;
    const u32 robotColor = state.status.connected ? colors_.success : colors_.danger;
    drawText(180, 10, 0.38f, bridgeColor, "%s", bridgeLabel(state).c_str());
    drawText(296, 10, 0.38f, robotColor, "%s", robotLabel(state).c_str());
    drawText(14, 28, 0.36f, colors_.textSoft, "Claim: %s | Last: %s",
             leaseSummary(state).c_str(), ellipsize(state.status.lastAction, 30).c_str());

    drawCard(6, 50, 388, 184, colors_.panelAlt);
    float y = 54.0f;
    constexpr float rowH = 28.0f;
    constexpr int maxRows = 6;
    const int start = std::max(0, static_cast<int>(state.transcript.size()) - maxRows - state.transcriptScroll);
    const int end = std::min(static_cast<int>(state.transcript.size()), start + maxRows);
    for (int i = start; i < end; ++i) {
        drawCard(10, y, 380, rowH - 2, i % 2 == 0 ? colors_.panel : colors_.mint);
        drawText(18, y + 5, 0.38f, colors_.text, "%s", ellipsize(state.transcript[static_cast<std::size_t>(i)], 66).c_str());
        y += rowH;
    }

    if (state.transcript.empty()) {
        drawText(14, 116, 0.44f, colors_.textSoft, "No Desk activity yet.");
        drawText(14, 140, 0.40f, colors_.textSoft, "Type a prompt and send it.");
    }
}

void UiRenderer::drawTopPilot(const AppState& state) {
    drawCard(6, 4, 388, 36, colors_.panel);
    drawText(14, 10, 0.50f, colors_.accent, "Pilot");
    const u32 bridgeColor = state.bridgeReachable ? colors_.success : colors_.danger;
    const u32 robotColor = state.status.connected ? colors_.success : colors_.danger;
    drawText(100, 12, 0.36f, bridgeColor, "%s", bridgeLabel(state).c_str());
    drawText(210, 12, 0.36f, robotColor, "%s", robotLabel(state).c_str());
    drawText(316, 12, 0.36f, colors_.textSoft, "Batt %s", batteryLabel(state.status.batteryPercent).c_str());

    drawCard(kImageCardX, kImageCardY, kImageCardW, kImageCardH, colors_.panelAlt);
    drawText(14, 46, 0.38f, colors_.accent, "Latest capture");
    if (state.preview.ready() && previewTextureReady_ && previewTextureSourceKey_ == state.preview.sourceKey) {
        drawCard(kPreviewFrameX, kPreviewFrameY, kPreviewFrameW, kPreviewFrameH, colors_.panel);
        const float scaleX = kPreviewFrameW / static_cast<float>(previewSubTexture_.width);
        const float scaleY = kPreviewFrameH / static_cast<float>(previewSubTexture_.height);
        C2D_DrawImageAt(previewImage_, kPreviewDrawX, kPreviewDrawY, 0.55f, nullptr, scaleX, scaleY);
        drawText(14, 214, 0.34f, colors_.textSoft, "%s",
                 state.status.latestImageCapturedAt.empty() ? "capture ready" : ellipsize(state.status.latestImageCapturedAt, 32).c_str());
    } else if (state.preview.ready()) {
        drawCard(kPreviewFrameX, kPreviewFrameY, kPreviewFrameW, kPreviewFrameH, colors_.panel);
        drawText(36, 118, 0.42f, colors_.textSoft, "Preview sync failed");
        drawText(36, 142, 0.36f, colors_.text, "Image cached, texture upload failed.");
    } else if (!state.status.latestImageUrl.empty()) {
        drawText(36, 118, 0.44f, colors_.textSoft, "Preview loading...");
        drawText(36, 142, 0.36f, colors_.text, "%s",
                 state.status.latestImageCapturedAt.empty() ? "capture ready on bridge" : ellipsize(state.status.latestImageCapturedAt, 34).c_str());
    } else {
        drawText(48, 114, 0.46f, colors_.textSoft, "No image yet");
        drawText(36, 140, 0.38f, colors_.textSoft, "Press Y or tap Capture.");
    }

    drawCard(252, 42, 142, 192, colors_.mint);
    drawText(260, 48, 0.40f, colors_.accent, "Telemetry");
    drawText(260, 68, 0.36f, colors_.text, "Host");
    drawText(260, 84, 0.34f, colors_.textSoft, "%s", ellipsize(state.status.robotHost, 16).c_str());
    drawText(260, 102, 0.36f, colors_.text, "Mode");
    drawText(260, 118, 0.34f, colors_.textSoft, "%s", ellipsize(state.status.runtimeMode, 16).c_str());
    drawText(260, 136, 0.36f, colors_.text, "Claim");
    drawText(260, 152, 0.34f, colors_.textSoft, "%s", leaseSummary(state).c_str());
    drawText(260, 170, 0.36f, colors_.text, "FSM");
    drawText(260, 186, 0.34f, colors_.textSoft, "%s", ellipsize(state.status.activeFsm, 16).c_str());
    drawText(260, 204, 0.36f, colors_.text, "Last");
    drawText(260, 218, 0.34f, colors_.textSoft, "%s", ellipsize(state.status.lastResult, 16).c_str());
}

void UiRenderer::drawBottomNav(const AppState& state) {
    bottomButtons_.clear();
    ButtonRect chat{8, 4, 148, 38, "tab-chat", "Chat"};
    ButtonRect pilot{164, 4, 148, 38, "tab-pilot", "Pilot"};
    drawButton(chat, state.context == BottomContext::Chat);
    drawButton(pilot, state.context == BottomContext::Pilot);
    bottomButtons_.push_back(chat);
    bottomButtons_.push_back(pilot);
}

void UiRenderer::drawBottomChat(const AppState& state) {
    drawCard(8, 50, 304, 90, colors_.panel);
    drawText(16, 56, 0.40f, colors_.accent, "Prompt draft");
    drawText(16, 78, 0.42f, colors_.text, "%s",
             state.composeText.empty() ? "Press Input to type a prompt." : ellipsize(state.composeText, 56).c_str());
    drawText(16, 118, 0.34f, colors_.textSoft, "Send posts to Asteria Desk.");

    ButtonRect input{8, 150, 146, 40, "input", "Input"};
    ButtonRect send{162, 150, 150, 40, "send", "Send"};
    drawButton(input, false);
    drawButton(send, true);
    bottomButtons_.push_back(input);
    bottomButtons_.push_back(send);
}

void UiRenderer::drawBottomPilot(const AppState& state) {
    const std::string claimLabel = state.status.leaseHolderId == state.config.holderId && state.status.leaseActive
        ? ("Held " + std::to_string(state.status.leaseSecondsRemaining) + "s")
        : "Claim";
    ButtonRect claim{8, 50, 110, 36, "claim", claimLabel};
    ButtonRect reconnect{124, 50, 96, 36, "reconnect", "Reconnect"};
    ButtonRect estop{226, 50, 86, 36, "stop", "E-Stop"};
    drawButton(claim, state.status.leaseHolderId == state.config.holderId && state.status.leaseActive);
    drawButton(reconnect, !state.status.connected && state.bridgeReachable);
    drawButton(estop, false, true);
    bottomButtons_.push_back(claim);
    bottomButtons_.push_back(reconnect);
    bottomButtons_.push_back(estop);

    constexpr float cx = 80.0f;
    constexpr float cy = 164.0f;
    constexpr float sz = 48.0f;
    constexpr float gap = 2.0f;
    ButtonRect up{cx - sz / 2, cy - sz / 2 - sz - gap, sz, sz, "up", "Up"};
    ButtonRect left{cx - sz / 2 - sz - gap, cy - sz / 2, sz, sz, "left", "Left"};
    ButtonRect stop{cx - sz / 2, cy - sz / 2, sz, sz, "center", "Stop"};
    ButtonRect right{cx + sz / 2 + gap, cy - sz / 2, sz, sz, "right", "Right"};
    ButtonRect down{cx - sz / 2, cy + sz / 2 + gap, sz, sz, "down", "Down"};
    drawButton(up, false);
    drawButton(left, false);
    drawButton(stop, false, true);
    drawButton(right, false);
    drawButton(down, false);
    bottomButtons_.push_back(up);
    bottomButtons_.push_back(left);
    bottomButtons_.push_back(stop);
    bottomButtons_.push_back(right);
    bottomButtons_.push_back(down);

    drawCard(164, 92, 148, 146, colors_.panelAlt);
    drawText(174, 98, 0.40f, colors_.accent, "Actions");
    ButtonRect grab{172, 118, 132, 34, "grab", "Pickup"};
    ButtonRect kick{172, 158, 132, 34, "kick", "Kick"};
    ButtonRect capture{172, 198, 132, 34, "capture", "Capture"};
    drawButton(grab, false);
    drawButton(kick, false);
    drawButton(capture, false);
    bottomButtons_.push_back(grab);
    bottomButtons_.push_back(kick);
    bottomButtons_.push_back(capture);
}

void UiRenderer::draw(const AppState& state) {
    if (!initialized_) return;
    if (state.context == BottomContext::Pilot && state.preview.ready()) {
        syncPreviewTexture(state.preview);
    } else if (!state.preview.ready()) {
        previewTextureReady_ = false;
        previewTextureSourceKey_.clear();
    }

    C2D_TargetClear(topTarget_, colors_.canvas);
    C2D_TargetClear(bottomTarget_, colors_.canvas);
    frameState_ = &state;

    C2D_SceneBegin(topTarget_);
    if (state.context == BottomContext::Chat) {
        drawTopChat(state);
    } else {
        drawTopPilot(state);
    }

    C2D_SceneBegin(bottomTarget_);
    drawBottomNav(state);
    if (state.context == BottomContext::Chat) {
        drawBottomChat(state);
    } else {
        drawBottomPilot(state);
    }
}

}  // namespace asteria
