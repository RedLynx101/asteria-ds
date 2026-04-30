#include "http_client.hpp"

#include <3ds.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace asteria {
namespace {

constexpr std::uint64_t kStatusTimeoutNs = 250000000ULL;
constexpr std::uint64_t kBinaryGetTimeoutNs = 1800000000ULL;
constexpr std::uint64_t kCommandTimeoutNs = 600000000ULL;

void closeContext(httpcContext& context, bool cancel) {
    if (cancel) {
        httpcCancelConnection(&context);
    }
    httpcCloseContext(&context);
}

}  // namespace

bool HttpClient::init() {
    httpReady_ = R_SUCCEEDED(httpcInit(4 * 1024 * 1024));
    acReady_ = R_SUCCEEDED(acInit());
    return httpReady_;
}

void HttpClient::shutdown() {
    if (httpReady_) {
        httpcExit();
        httpReady_ = false;
    }
    if (acReady_) {
        acExit();
        acReady_ = false;
    }
}

HttpResponse HttpClient::get(const std::string& url, const std::string& bearerToken) {
    return request("GET", url, bearerToken, "", kStatusTimeoutNs, "application/json");
}

HttpResponse HttpClient::getBinary(const std::string& url, const std::string& bearerToken) {
    return request("GET", url, bearerToken, "", kBinaryGetTimeoutNs, "*/*");
}

HttpResponse HttpClient::postJson(const std::string& url, const std::string& bearerToken, const std::string& jsonBody) {
    return request("POST", url, bearerToken, jsonBody, kCommandTimeoutNs, "application/json");
}

bool HttpClient::wifiConnected() const {
    if (!acReady_) return true;

    u32 status = 0;
    if (R_FAILED(ACU_GetStatus(&status))) return true;
    return status == 3;
}

HttpResponse HttpClient::request(const std::string& method, const std::string& url, const std::string& bearerToken, const std::string& body,
                                 std::uint64_t timeoutNs, const std::string& acceptHeader) {
    HttpResponse result;
    if (!httpReady_ || url.empty() || !wifiConnected()) {
        return result;
    }

    httpcContext context{};
    HTTPC_RequestMethod requestMethod = method == "POST" ? HTTPC_METHOD_POST : HTTPC_METHOD_GET;
    std::vector<u32> postBuffer;

    if (R_FAILED(httpcOpenContext(&context, requestMethod, url.c_str(), 0))) {
        return result;
    }

    httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
    httpcAddRequestHeaderField(&context, "User-Agent", "AsteriaDS/0.1");
    httpcAddRequestHeaderField(&context, "Accept", acceptHeader.c_str());

    if (!bearerToken.empty()) {
        const std::string auth = "Bearer " + bearerToken;
        httpcAddRequestHeaderField(&context, "Authorization", auth.c_str());
    }

    if (requestMethod == HTTPC_METHOD_POST) {
        httpcAddRequestHeaderField(&context, "Content-Type", "application/json");
        postBuffer.assign((body.size() + sizeof(u32) - 1) / sizeof(u32), 0);
        if (!body.empty()) {
            std::memcpy(postBuffer.data(), body.data(), body.size());
            httpcAddPostDataRaw(&context, postBuffer.data(), body.size());
        }
    }

    if (R_FAILED(httpcBeginRequest(&context))) {
        closeContext(context, true);
        return result;
    }

    u32 statusCode = 0;
    const Result statusResult = httpcGetResponseStatusCodeTimeout(&context, &statusCode, timeoutNs);
    if (R_FAILED(statusResult)) {
        closeContext(context, true);
        return result;
    }
    result.statusCode = static_cast<long>(statusCode);

    u32 downloadSize = 0;
    if (R_FAILED(httpcGetDownloadSizeState(&context, nullptr, &downloadSize))) {
        closeContext(context, true);
        return result;
    }

    if (downloadSize > 0) {
        std::vector<u8> buffer(downloadSize);
        u32 downloaded = 0;
        while (downloaded < downloadSize) {
            const u32 before = downloaded;
            const u32 chunkSize = std::min<u32>(downloadSize - downloaded, 4096u);
            const Result readResult = httpcReceiveDataTimeout(&context, buffer.data() + downloaded, chunkSize, timeoutNs);
            if (R_FAILED(readResult) && readResult != static_cast<Result>(HTTPC_RESULTCODE_DOWNLOADPENDING)) {
                closeContext(context, true);
                return result;
            }
            u32 updatedDownloaded = downloaded;
            u32 updatedSize = downloadSize;
            if (R_FAILED(httpcGetDownloadSizeState(&context, &updatedDownloaded, &updatedSize))) {
                closeContext(context, true);
                return result;
            }
            downloaded = updatedDownloaded;
            downloadSize = updatedSize;
            if (downloaded <= before && readResult == static_cast<Result>(HTTPC_RESULTCODE_DOWNLOADPENDING)) {
                svcSleepThread(5 * 1000 * 1000);
            }
        }
        result.body.assign(reinterpret_cast<char*>(buffer.data()), reinterpret_cast<char*>(buffer.data()) + downloaded);
    }

    closeContext(context, false);
    return result;
}

std::string jsonString(const std::string& input) {
    std::string escaped;
    escaped.reserve(input.size() + 8);
    for (char ch : input) {
        if (ch == '"' || ch == '\\') escaped.push_back('\\');
        if (ch == '\n') {
            escaped += "\\n";
            continue;
        }
        escaped.push_back(ch);
    }
    return "\"" + escaped + "\"";
}

std::string jsonBodyForTeleop(float forward, float turn, float strafe) {
    return "{"
           "\"forward\":" + std::to_string(forward) + ","
           "\"turn\":" + std::to_string(turn) + ","
           "\"strafe\":" + std::to_string(strafe) + ","
           "\"ttl_ms\":300"
           "}";
}

std::string jsonBodyForCommand(const std::string& command, const std::string& extra) {
    std::string body = "{"
                       "\"command\":" + jsonString(command);
    if (!extra.empty()) {
        body += "," + extra;
    }
    body += "}";
    return body;
}

}  // namespace asteria
