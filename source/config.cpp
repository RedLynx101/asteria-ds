#include "config.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace asteria {
namespace {

std::string findString(const std::string& text, const std::string& key, const std::string& fallback) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t keyPos = text.find(needle);
    if (keyPos == std::string::npos) return fallback;
    const std::size_t colon = text.find(':', keyPos);
    const std::size_t firstQuote = text.find('"', colon + 1);
    const std::size_t secondQuote = text.find('"', firstQuote + 1);
    if (firstQuote == std::string::npos || secondQuote == std::string::npos) return fallback;
    return text.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

bool findBool(const std::string& text, const std::string& key, bool fallback) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t keyPos = text.find(needle);
    if (keyPos == std::string::npos) return fallback;
    const std::size_t colon = text.find(':', keyPos);
    const std::size_t start = text.find_first_not_of(" \t\n\r", colon + 1);
    if (start == std::string::npos) return fallback;
    if (text.compare(start, 4, "true") == 0) return true;
    if (text.compare(start, 5, "false") == 0) return false;
    return fallback;
}

KickStyle parseKickStyle(const std::string& value) {
    if (value == "soft" || value == "Soft") return KickStyle::Soft;
    if (value == "hard" || value == "Hard") return KickStyle::Hard;
    return KickStyle::Medium;
}

std::string kickStyleValue(KickStyle value) {
    switch (value) {
        case KickStyle::Soft: return "soft";
        case KickStyle::Hard: return "hard";
        case KickStyle::Medium:
        default:
            return "medium";
    }
}

}  // namespace

bool fileExists(const std::string& path) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
}

std::string readTextFile(const std::string& path) {
    std::ifstream input(path);
    std::stringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool writeTextFile(const std::string& path, const std::string& content) {
    std::ofstream output(path);
    if (!output.good()) return false;
    output << content;
    return output.good();
}

bool loadConfig(const std::string& path, AppConfig& outConfig) {
    if (!fileExists(path)) return false;

    const std::string text = readTextFile(path);
    outConfig.daemonBaseUrl = findString(text, "daemon_base_url", outConfig.daemonBaseUrl);
    outConfig.deviceToken = findString(text, "device_token", outConfig.deviceToken);
    outConfig.holderId = findString(text, "holder_id", outConfig.holderId);
    outConfig.holderLabel = findString(text, "holder_label", outConfig.holderLabel);
    outConfig.theme = findString(text, "theme", outConfig.theme);
    outConfig.soundsEnabled = findBool(text, "sounds_enabled", outConfig.soundsEnabled);
    outConfig.kickStyle = parseKickStyle(findString(text, "kick_style", "medium"));
    return true;
}

bool saveConfig(const std::string& path, const AppConfig& config) {
    const std::string json =
        "{\n"
        "  \"daemon_base_url\": \"" + config.daemonBaseUrl + "\",\n"
        "  \"device_token\": \"" + config.deviceToken + "\",\n"
        "  \"holder_id\": \"" + config.holderId + "\",\n"
        "  \"holder_label\": \"" + config.holderLabel + "\",\n"
        "  \"theme\": \"" + config.theme + "\",\n"
        "  \"sounds_enabled\": " + std::string(config.soundsEnabled ? "true" : "false") + ",\n"
        "  \"kick_style\": \"" + kickStyleValue(config.kickStyle) + "\"\n"
        "}\n";
    return writeTextFile(path, json);
}

}  // namespace asteria
