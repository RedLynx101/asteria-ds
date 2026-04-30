#pragma once

#include <string>

#include "models.hpp"

namespace asteria {

bool fileExists(const std::string& path);
std::string readTextFile(const std::string& path);
bool writeTextFile(const std::string& path, const std::string& content);
bool loadConfig(const std::string& path, AppConfig& outConfig);
bool saveConfig(const std::string& path, const AppConfig& config);

}  // namespace asteria
