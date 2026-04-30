#pragma once

#include <cstdint>
#include <string>

namespace asteria {

struct HttpResponse {
    long statusCode = 0;
    std::string body;
};

class HttpClient {
  public:
    bool init();
    void shutdown();
    bool wifiConnected() const;

    HttpResponse get(const std::string& url, const std::string& bearerToken);
    HttpResponse getBinary(const std::string& url, const std::string& bearerToken);
    HttpResponse postJson(const std::string& url, const std::string& bearerToken, const std::string& jsonBody);

  private:
    HttpResponse request(const std::string& method, const std::string& url, const std::string& bearerToken, const std::string& body,
                         std::uint64_t timeoutNs, const std::string& acceptHeader);

    bool httpReady_ = false;
    bool acReady_ = false;
};

std::string jsonString(const std::string& input);
std::string jsonBodyForTeleop(float forward, float turn, float strafe);
std::string jsonBodyForCommand(const std::string& command, const std::string& extra = "");

}  // namespace asteria
