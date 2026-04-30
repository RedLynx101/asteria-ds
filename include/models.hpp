#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace asteria {

enum class BottomContext {
    Chat,
    Pilot
};

enum class KickStyle {
    Soft,
    Medium,
    Hard
};

struct SessionSummary {
    std::string id;
    std::string title;
    std::string state;
    std::string lastPreview;
};

struct RemoteStatus {
    bool connected = false;
    bool manualControlAllowed = false;
    bool supportsFsmRuntime = false;
    bool leaseActive = false;
    std::string runtimeMode = "idle";
    std::string leaseHolder = "Unclaimed";
    std::string leaseHolderId;
    std::string leaseHolderKind = "available";
    std::string robotHost = "n/a";
    std::string activeFsm = "n/a";
    std::string lastAction = "Waiting";
    std::string lastResult = "n/a";
    std::string latestImagePreviewUrl;
    int batteryPercent = -1;
    int leaseSecondsRemaining = 0;
    int poseXmm = 0;
    int poseYmm = 0;
    int headingDeg = 0;
    std::string latestImageUrl;
    std::string latestImageCapturedAt;
};

struct ImagePreview {
    int width = 0;
    int height = 0;
    std::string sourceKey;
    std::vector<std::uint8_t> rgb565;

    bool ready() const {
        return width > 0 && height > 0 && rgb565.size() == static_cast<std::size_t>(width * height * 2);
    }

    void clear() {
        width = 0;
        height = 0;
        sourceKey.clear();
        rgb565.clear();
    }
};

struct AppConfig {
    std::string daemonBaseUrl = "http://127.0.0.1:8766";
    std::string deviceToken;
    std::string holderId = "asteria-ds";
    std::string holderLabel = "Asteria DS";
    std::string theme = "light_asteria";
    bool soundsEnabled = true;
    KickStyle kickStyle = KickStyle::Medium;
};

struct TeleopVector {
    float forward = 0.0f;
    float turn = 0.0f;
    float strafe = 0.0f;
};

struct ButtonRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    std::string id;
    std::string label;

    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct AppState {
    BottomContext context = BottomContext::Chat;
    bool shouldQuit = false;
    bool connected = false;
    bool bridgeReachable = false;
    bool pilotBlocked = false;
    bool teleopActive = false;
    bool loading = false;
    int selectedSession = 0;
    int transcriptScroll = 0;
    RemoteStatus status;
    AppConfig config;
    std::vector<SessionSummary> sessions;
    std::vector<std::string> transcript;
    std::string composeText;
    TeleopVector teleop;
    TeleopVector touchTeleop;
    TeleopVector physicalTeleop;
    std::string lastTeleopDebugTag;
    ImagePreview preview;
};

inline const char* kickStyleLabel(KickStyle style) {
    switch (style) {
        case KickStyle::Soft: return "Soft";
        case KickStyle::Medium: return "Medium";
        case KickStyle::Hard: return "Hard";
        default: return "Medium";
    }
}

}  // namespace asteria
