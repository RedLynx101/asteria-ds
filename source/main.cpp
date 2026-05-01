#include <3ds.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>

#include "audio_manager.hpp"
#include "config.hpp"
#include "http_client.hpp"
#include "models.hpp"
#include "ui.hpp"

namespace asteria {
namespace {

constexpr const char* kConfigPath = "sdmc:/3ds/asteria-ds/config.json";
constexpr std::uint64_t kStatusPollMs = 1500;
constexpr std::uint64_t kTeleopSendMs = 120;
constexpr int kPreviewWidth = 176;
constexpr int kPreviewHeight = 132;
constexpr std::size_t kTranscriptLimit = 36;

struct CommandFeedback {
    bool ok = false;
    std::string detail;
};

std::size_t skipWhitespace(const std::string& text, std::size_t index) {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
    }
    return index;
}

std::size_t findValueStart(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t keyPos = text.find(needle);
    if (keyPos == std::string::npos) return std::string::npos;
    const std::size_t colon = text.find(':', keyPos + needle.size());
    if (colon == std::string::npos) return std::string::npos;
    return skipWhitespace(text, colon + 1);
}

std::string findStringValue(const std::string& text, const std::string& key, const std::string& fallback) {
    std::size_t start = findValueStart(text, key);
    if (start == std::string::npos || start >= text.size() || text[start] != '"') {
        return fallback;
    }
    ++start;

    std::string value;
    value.reserve(64);
    bool escape = false;
    for (std::size_t i = start; i < text.size(); ++i) {
        const char ch = text[i];
        if (escape) {
            switch (ch) {
                case '"':
                case '\\':
                case '/':
                    value.push_back(ch);
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    value.push_back(ch);
                    break;
            }
            escape = false;
            continue;
        }
        if (ch == '\\') {
            escape = true;
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value.push_back(ch);
    }
    return fallback;
}

bool findBoolValue(const std::string& text, const std::string& key, bool fallback) {
    const std::size_t start = findValueStart(text, key);
    if (start == std::string::npos) return fallback;
    if (text.compare(start, 4, "true") == 0) return true;
    if (text.compare(start, 5, "false") == 0) return false;
    return fallback;
}

int findIntValue(const std::string& text, const std::string& key, int fallback) {
    const std::size_t start = findValueStart(text, key);
    if (start == std::string::npos || start >= text.size()) return fallback;
    if (!(text[start] == '-' || std::isdigit(static_cast<unsigned char>(text[start])) != 0)) {
        return fallback;
    }
    std::size_t end = start + 1;
    while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])) != 0) {
        ++end;
    }
    return std::atoi(text.substr(start, end - start).c_str());
}

std::string ellipsize(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) return text;
    if (limit <= 3) return text.substr(0, limit);
    return text.substr(0, limit - 3) + "...";
}

void pushTranscript(AppState& state, const std::string& message) {
    if (message.empty()) return;
    const std::string compact = ellipsize(message, 100);
    if (!state.transcript.empty() && state.transcript.back() == compact) return;
    state.transcript.push_back(compact);
    if (state.transcript.size() > kTranscriptLimit) {
        state.transcript.erase(state.transcript.begin());
    }
}

std::string joinUrl(const std::string& base, const std::string& path) {
    if (base.empty()) return path;
    if (path.empty()) return base;
    if (base.back() == '/' && path.front() == '/') return base.substr(0, base.size() - 1) + path;
    if (base.back() != '/' && path.front() != '/') return base + path;
    return base + path;
}

void showFatalError(const std::string& message) {
    gfxInitDefault();
    consoleInit(GFX_TOP, nullptr);
    std::printf("Asteria DS init failed:\n%s\n\nPress START to exit.\n", message.c_str());
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) {
            break;
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    gfxExit();
}

void seedUi(AppState& state) {
    state.sessions.clear();
    state.transcript = {
        "Asteria: Ready for Desk prompts.",
        "System: Chat tab now sends directly to the Asteria Desk.",
        "System: Pilot tab mirrors claim, reconnect, kick, capture, and teleop.",
    };
}

std::string previewSourceKey(const RemoteStatus& status) {
    if (!status.latestImageCapturedAt.empty()) return status.latestImageCapturedAt;
    return status.latestImageUrl;
}

void clearPreview(AppState& state) {
    state.preview.clear();
}

bool fetchLatestPreview(HttpClient& http, AppState& state) {
    if (state.status.latestImagePreviewUrl.empty() || state.status.latestImageUrl.empty()) {
        clearPreview(state);
        return false;
    }

    const std::string sourceKey = previewSourceKey(state.status);
    if (state.preview.ready() && state.preview.sourceKey == sourceKey) {
        return true;
    }

    const std::string baseUrl = joinUrl(state.config.daemonBaseUrl, state.status.latestImagePreviewUrl);
    const std::string url = baseUrl +
                            (baseUrl.find('?') == std::string::npos ? "?" : "&") +
                            "stamp=" + std::to_string(static_cast<long long>(osGetTime()));
    const auto response = http.getBinary(url, state.config.deviceToken);
    const std::size_t expectedSize = static_cast<std::size_t>(kPreviewWidth * kPreviewHeight * 2);
    if (response.statusCode >= 200 && response.statusCode < 300 && response.body.size() == expectedSize) {
        state.preview.width = kPreviewWidth;
        state.preview.height = kPreviewHeight;
        state.preview.sourceKey = sourceKey;
        state.preview.rgb565.assign(response.body.begin(), response.body.end());
        return true;
    }

    clearPreview(state);
    return false;
}

void parseStatusJson(const std::string& body, AppState& state, HttpClient& http) {
    state.bridgeReachable = true;
    state.connected = findBoolValue(body, "connected", false);
    state.status.connected = state.connected;
    state.status.manualControlAllowed = findBoolValue(body, "manual_control_allowed", false);
    state.status.supportsFsmRuntime = findBoolValue(body, "supports_fsm_runtime", false);
    state.status.leaseActive = findBoolValue(body, "lease_active", false);
    state.status.runtimeMode = findStringValue(body, "runtime_mode", "idle");
    state.status.leaseHolder = findStringValue(body, "lease_holder", "Unclaimed");
    state.status.leaseHolderId = findStringValue(body, "lease_holder_id", "");
    state.status.leaseHolderKind = findStringValue(body, "lease_holder_kind", "available");
    state.status.robotHost = findStringValue(body, "robot_host", "n/a");
    state.status.activeFsm = findStringValue(body, "active_fsm", "n/a");
    state.status.lastAction = findStringValue(body, "last_action", "Waiting");
    state.status.lastResult = findStringValue(body, "last_result", state.connected ? "ready" : "offline");
    state.status.latestImageUrl = findStringValue(body, "url", "");
    state.status.latestImageCapturedAt = findStringValue(body, "captured_at", "");
    state.status.latestImagePreviewUrl = findStringValue(body, "latest_image_preview_url", "");
    state.status.batteryPercent = findIntValue(body, "battery_percent", -1);
    state.status.leaseSecondsRemaining = findIntValue(body, "lease_seconds_remaining", 0);
    state.status.poseXmm = findIntValue(body, "x_mm", 0);
    state.status.poseYmm = findIntValue(body, "y_mm", 0);
    state.status.headingDeg = findIntValue(body, "heading_deg", 0);
    state.pilotBlocked = !state.status.manualControlAllowed && state.status.leaseHolderId != state.config.holderId;
    if (!state.status.latestImageUrl.empty()) {
        fetchLatestPreview(http, state);
    } else {
        clearPreview(state);
    }
}

bool refreshStatus(HttpClient& http, AppState& state) {
    const auto response = http.get(joinUrl(state.config.daemonBaseUrl, "/api/mobile/status"), state.config.deviceToken);
    if (response.statusCode >= 200 && response.statusCode < 300 && !response.body.empty()) {
        parseStatusJson(response.body, state, http);
        return true;
    }

    state.bridgeReachable = false;
    state.connected = false;
    state.status.connected = false;
    state.status.lastAction = http.wifiConnected() ? "Bridge unreachable" : "Wi-Fi offline";
    state.status.lastResult = http.wifiConnected() ? "Bridge unreachable" : "Wi-Fi offline";
    state.status.runtimeMode = "idle";
    clearPreview(state);
    return false;
}

CommandFeedback summarizeCommandResponse(const HttpResponse& response, const std::string& successFallback, const std::string& failureFallback) {
    CommandFeedback feedback;
    const bool httpOk = response.statusCode >= 200 && response.statusCode < 300;
    const bool okFlag = findBoolValue(response.body, "ok", httpOk);
    const bool granted = findBoolValue(response.body, "granted", true);
    const bool released = findBoolValue(response.body, "released", true);
    const std::string blocked = findStringValue(response.body, "blocked_reason", "");
    const std::string error = findStringValue(response.body, "error", "");
    const std::string message = findStringValue(response.body, "message", "");
    const std::string warning = findStringValue(response.body, "warning", "");

    feedback.ok = httpOk && okFlag && granted && released && blocked.empty() && error.empty();
    if (!blocked.empty()) {
        feedback.detail = blocked;
    } else if (!error.empty()) {
        feedback.detail = error;
    } else if (!warning.empty() && !message.empty()) {
        feedback.detail = message + " | " + warning;
    } else if (!warning.empty()) {
        feedback.detail = warning;
    } else if (!message.empty()) {
        feedback.detail = message;
    } else {
        feedback.detail = feedback.ok ? successFallback : failureFallback;
    }
    return feedback;
}

void fireCommand(
    HttpClient& http,
    AppState& state,
    const std::string& endpoint,
    const std::string& label,
    const std::string& body = "{}",
    bool refreshAfter = true
) {
    const auto response = http.postJson(joinUrl(state.config.daemonBaseUrl, endpoint), state.config.deviceToken, body);
    const CommandFeedback feedback = summarizeCommandResponse(response, label + " ok", label + " failed");
    pushTranscript(state, (feedback.ok ? "System: " : "Error: ") + label + " - " + feedback.detail);
    if (refreshAfter) {
        refreshStatus(http, state);
    }
}

std::string trimWhitespace(std::string value) {
    const auto start = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto finish = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (start >= finish) return {};
    return std::string(start, finish);
}

bool openComposeKeyboard(AppState& state) {
    SwkbdState keyboard;
    swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 240);
    swkbdSetHintText(&keyboard, "Prompt for Asteria Desk");
    swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "OK", true);
    swkbdSetValidation(&keyboard, SWKBD_ANYTHING, 0, 0);
    swkbdSetFeatures(&keyboard, SWKBD_DARKEN_TOP_SCREEN | SWKBD_PREDICTIVE_INPUT | SWKBD_MULTILINE);
    if (!state.composeText.empty()) {
        swkbdSetInitialText(&keyboard, state.composeText.c_str());
    }

    char buffer[256] = {};
    if (!state.composeText.empty()) {
        std::snprintf(buffer, sizeof(buffer), "%s", state.composeText.c_str());
    }

    const SwkbdButton button = swkbdInputText(&keyboard, buffer, sizeof(buffer));
    if (button != SWKBD_BUTTON_CONFIRM) {
        return false;
    }

    state.composeText = trimWhitespace(buffer);
    return true;
}

void submitDeskPrompt(HttpClient& http, AppState& state) {
    const std::string promptText = trimWhitespace(state.composeText);
    if (promptText.empty()) {
        pushTranscript(state, "System: Enter a Desk prompt first.");
        return;
    }

    pushTranscript(state, "You: " + ellipsize(promptText, 88));
    const auto response = http.postJson(
        joinUrl(state.config.daemonBaseUrl, "/api/mobile/prompt"),
        state.config.deviceToken,
        "{"
        "\"text\":" + jsonString(promptText) + ","
        "\"holder_id\":" + jsonString(state.config.holderId) + ","
        "\"holder_label\":" + jsonString(state.config.holderLabel) +
        "}"
    );
    const CommandFeedback feedback = summarizeCommandResponse(response, "prompt submitted", "prompt submit failed");
    pushTranscript(state, (feedback.ok ? "Desk: " : "Error: Desk - ") + feedback.detail);
    if (feedback.ok) {
        state.composeText.clear();
    }
    refreshStatus(http, state);
}

bool isTouchPilotPad(const std::string& id) {
    return id == "up" || id == "down" || id == "left" || id == "right";
}

TeleopVector touchVectorForButton(const std::string& id) {
    TeleopVector vector;
    if (id == "up") vector.forward = 0.8f;
    if (id == "down") vector.forward = -0.8f;
    if (id == "left") vector.turn = -0.7f;
    if (id == "right") vector.turn = 0.7f;
    return vector;
}

std::string describeTeleop(const TeleopVector& vector) {
    const float absForward = std::fabs(vector.forward);
    const float absTurn = std::fabs(vector.turn);
    const float absStrafe = std::fabs(vector.strafe);
    if (absForward < 0.01f && absTurn < 0.01f && absStrafe < 0.01f) {
        return "";
    }
    if (absForward >= absTurn && absForward >= absStrafe) {
        return vector.forward >= 0.0f ? "Drive forward" : "Drive backward";
    }
    if (absTurn >= absForward && absTurn >= absStrafe) {
        return vector.turn >= 0.0f ? "Turn right" : "Turn left";
    }
    return vector.strafe >= 0.0f ? "Strafe right" : "Strafe left";
}

bool teleopHasMotion(const TeleopVector& vector) {
    return std::fabs(vector.forward) > 0.01f || std::fabs(vector.turn) > 0.01f || std::fabs(vector.strafe) > 0.01f;
}

void firePickupFallback(HttpClient& http, AppState& state) {
    fireCommand(
        http,
        state,
        "/api/mobile/teleop/command",
        "Pickup",
        jsonBodyForCommand("grab_assist")
    );
}

TeleopVector mergedTeleop(const AppState& state) {
    TeleopVector merged = state.physicalTeleop;
    if (std::fabs(state.touchTeleop.forward) > 0.01f) merged.forward = state.touchTeleop.forward;
    if (std::fabs(state.touchTeleop.turn) > 0.01f) merged.turn = state.touchTeleop.turn;
    if (std::fabs(state.touchTeleop.strafe) > 0.01f) merged.strafe = state.touchTeleop.strafe;
    return merged;
}

void handleButtonAction(const ButtonRect& button, HttpClient& http, AppState& state, AudioManager& audio) {
    if (button.id == "tab-chat") {
        state.context = BottomContext::Chat;
        audio.play(Cue::Tab);
        return;
    }
    if (button.id == "tab-pilot") {
        state.context = BottomContext::Pilot;
        audio.play(Cue::Tab);
        return;
    }
    if (button.id == "input") {
        if (openComposeKeyboard(state)) {
            pushTranscript(state, "System: Prompt draft updated.");
        }
        audio.play(Cue::Send);
        return;
    }
    if (button.id == "send") {
        submitDeskPrompt(http, state);
        audio.play(Cue::Send);
        return;
    }
    if (button.id == "claim") {
        fireCommand(http, state, "/api/mobile/teleop/claim", "Claim control",
                    "{\"holder_id\":" + jsonString(state.config.holderId) + ",\"holder_label\":" + jsonString(state.config.holderLabel) + "}");
        audio.play(Cue::Send);
        return;
    }
    if (button.id == "reconnect") {
        fireCommand(http, state, "/api/mobile/teleop/command", "Reconnect robot", jsonBodyForCommand("reconnect"));
        audio.play(Cue::Send);
        return;
    }
    if (button.id == "stop" || button.id == "center") {
        fireCommand(http, state, "/api/mobile/teleop/stop", "Stop all");
        state.touchTeleop = {};
        state.physicalTeleop = {};
        state.teleop = {};
        state.teleopActive = false;
        state.lastTeleopDebugTag.clear();
        audio.play(Cue::Send);
        return;
    }
    if (button.id == "grab") {
        firePickupFallback(http, state);
        audio.play(Cue::Send);
        return;
    }
    if (button.id == "kick") {
        fireCommand(http, state, "/api/mobile/teleop/command", "Kick",
                    jsonBodyForCommand("kick", "\"style\":\"" + std::string(kickStyleLabel(state.config.kickStyle)) + "\""));
        audio.play(Cue::Send);
        return;
    }
    if (button.id == "capture") {
        fireCommand(http, state, "/api/mobile/images/capture", "Capture image");
        audio.play(Cue::Capture);
        return;
    }
}

bool handleTouchInput(
    const touchPosition& touch,
    u32 keyDownMask,
    u32 keyHeldMask,
    const std::vector<ButtonRect>& buttons,
    HttpClient& http,
    AppState& state,
    AudioManager& audio
) {
    bool forceTeleopDispatch = false;
    state.touchTeleop = {};
    if ((keyHeldMask & KEY_TOUCH) == 0) {
        return false;
    }

    for (const auto& button : buttons) {
        if (!button.contains(static_cast<float>(touch.px), static_cast<float>(touch.py))) {
            continue;
        }

        if (isTouchPilotPad(button.id) && state.context == BottomContext::Pilot) {
            state.touchTeleop = touchVectorForButton(button.id);
            if (keyDownMask & KEY_TOUCH) {
                forceTeleopDispatch = true;
                state.lastTeleopDebugTag.clear();
            }
            return forceTeleopDispatch;
        }

        if (keyDownMask & KEY_TOUCH) {
            handleButtonAction(button, http, state, audio);
        }
        return forceTeleopDispatch;
    }

    return forceTeleopDispatch;
}

void handlePhysicalInput(u32 held, u32 down, HttpClient& http, AppState& state, AudioManager& audio) {
    if (down & KEY_SELECT) state.shouldQuit = true;
    if (down & KEY_START) {
        state.context = state.context == BottomContext::Chat ? BottomContext::Pilot : BottomContext::Chat;
        audio.play(Cue::Tab);
    }

    state.physicalTeleop = {};
    if (state.context != BottomContext::Pilot) return;

    if (down & KEY_A) {
        firePickupFallback(http, state);
        audio.play(Cue::Send);
    }
    if (down & KEY_B) {
        fireCommand(http, state, "/api/mobile/teleop/stop", "Stop all");
        state.teleopActive = false;
        state.lastTeleopDebugTag.clear();
        audio.play(Cue::Send);
    }
    if (down & KEY_X) {
        fireCommand(http, state, "/api/mobile/teleop/command", "Kick", jsonBodyForCommand("kick", "\"style\":\"medium\""));
        audio.play(Cue::Send);
    }
    if (down & KEY_Y) {
        fireCommand(http, state, "/api/mobile/images/capture", "Capture image");
        audio.play(Cue::Capture);
    }

    if (held & KEY_UP) state.physicalTeleop.forward = 0.8f;
    if (held & KEY_DOWN) state.physicalTeleop.forward = -0.8f;
    if (held & KEY_LEFT) state.physicalTeleop.turn = -0.7f;
    if (held & KEY_RIGHT) state.physicalTeleop.turn = 0.7f;
    if (held & KEY_L) state.physicalTeleop.strafe = -0.75f;
    if (held & KEY_R) state.physicalTeleop.strafe = 0.75f;

    circlePosition circle{};
    hidCircleRead(&circle);
    const float deadzone = 18.0f;
    if (std::abs(circle.dy) > deadzone) state.physicalTeleop.forward = std::clamp(circle.dy / 150.0f, -1.0f, 1.0f);
    if (std::abs(circle.dx) > deadzone) state.physicalTeleop.turn = std::clamp(circle.dx / 150.0f, -1.0f, 1.0f);
}

}  // namespace
}  // namespace asteria

int main(int argc, char** argv) {
    using namespace asteria;
    (void)argc;
    (void)argv;

    AppState state;
    loadConfig(kConfigPath, state.config);
    seedUi(state);

    UiRenderer ui;
    if (!ui.init()) {
        showFatalError(ui.lastError().empty() ? "Unknown UI error" : ui.lastError());
        return 1;
    }

    HttpClient http;
    const bool httpReady = http.init();
    if (!httpReady) {
        pushTranscript(state, "System: HTTP service failed to initialize.");
    }

    AudioManager audio;
    audio.init(state.config.soundsEnabled);
    audio.play(Cue::Boot);

    refreshStatus(http, state);

    const std::uint64_t startTime = osGetTime();
    std::uint64_t lastStatusPoll = startTime;
    std::uint64_t lastTeleopSend = 0;

    while (aptMainLoop() && !state.shouldQuit) {
        hidScanInput();
        const u32 keyDownMask = hidKeysDown();
        const u32 keyHeldMask = hidKeysHeld();

        bool forceTeleopDispatch = false;
        if (keyHeldMask & KEY_TOUCH) {
            touchPosition touch{};
            hidTouchRead(&touch);
            forceTeleopDispatch = handleTouchInput(touch, keyDownMask, keyHeldMask, ui.bottomButtons(), http, state, audio);
        } else {
            state.touchTeleop = {};
        }

        handlePhysicalInput(keyHeldMask, keyDownMask, http, state, audio);

        const std::uint64_t now = osGetTime();
        if (now - lastStatusPoll >= kStatusPollMs) {
            refreshStatus(http, state);
            lastStatusPoll = now;
        }

        audio.update();

        state.teleop = mergedTeleop(state);
        const std::string teleopTag = describeTeleop(state.teleop);
        if (!teleopTag.empty() && teleopTag != state.lastTeleopDebugTag) {
            pushTranscript(state, "Pilot: " + teleopTag);
            state.lastTeleopDebugTag = teleopTag;
        } else if (teleopTag.empty()) {
            state.lastTeleopDebugTag.clear();
        }

        const bool teleopMoving = state.context == BottomContext::Pilot && teleopHasMotion(state.teleop);
        if (teleopMoving && (forceTeleopDispatch || now - lastTeleopSend >= kTeleopSendMs)) {
            const auto response = http.postJson(
                joinUrl(state.config.daemonBaseUrl, "/api/mobile/teleop/vector"),
                state.config.deviceToken,
                jsonBodyForTeleop(state.teleop.forward, state.teleop.turn, state.teleop.strafe)
            );
            const CommandFeedback feedback = summarizeCommandResponse(response, "teleop vector sent", "teleop vector failed");
            if (!feedback.ok) {
                pushTranscript(state, "Error: Teleop - " + feedback.detail);
            }
            state.teleopActive = feedback.ok;
            lastTeleopSend = now;
        } else if (!teleopMoving && state.teleopActive && now - lastTeleopSend >= 80) {
            const auto response = http.postJson(
                joinUrl(state.config.daemonBaseUrl, "/api/mobile/teleop/stop"),
                state.config.deviceToken,
                "{}"
            );
            const CommandFeedback feedback = summarizeCommandResponse(response, "teleop stopped", "teleop stop failed");
            if (!feedback.ok) {
                pushTranscript(state, "Error: Teleop stop - " + feedback.detail);
            }
            state.teleopActive = false;
            lastTeleopSend = now;
        }

        ui.beginFrame();
        ui.draw(state);
        ui.endFrame();
    }

    saveConfig(kConfigPath, state.config);
    audio.shutdown();
    http.shutdown();
    ui.shutdown();
    return 0;
}
