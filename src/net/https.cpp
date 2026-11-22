#include "net/https.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

#include "core/device.h"
#include "net/root_certs.h"

namespace https {
namespace {

constexpr uint32_t TIMEOUT_MS = 10000;

bool startsWith(const char* s, const char* prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

String joinUrl(const char* base, const char* path) {
    String url(base);
    while (url.endsWith("/")) url.remove(url.length() - 1);
    url += path;
    return url;
}

Response send(HTTPClient& http, WiFiClientSecure* tls, const char* method, const char* body, const char* secret) {
    Response r;
    http.setReuse(false);
    http.setTimeout(TIMEOUT_MS);
    http.setConnectTimeout(TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    if (secret) {
        String auth("Device ");
        auth += secret;
        http.addHeader("Authorization", auth);
        http.addHeader("X-Device-Id", device::hardwareId());
    }

    const int code = body ? http.sendRequest(method, (uint8_t*)body, strlen(body)) : http.sendRequest(method);
    if (code > 0) {
        r.status = code;
        r.body = http.getString();
    } else {
        char tlsError[96] = "";
        const int tlsCode = tls ? tls->lastError(tlsError, sizeof(tlsError)) : 0;
        r.error = tlsCode != 0 ? Error::Tls : Error::Unreachable;
        log_w("HTTP %s failed: %s%s%s", method, HTTPClient::errorToString(code).c_str(), tlsCode ? ", TLS: " : "",
              tlsError);
    }
    http.end();
    return r;
}

}  // namespace

bool urlAllowed(const char* base) {
    if (base == nullptr) return false;
    if (startsWith(base, "https://")) return strlen(base) > 8;
#ifdef TELOS_DEBUG
    if (startsWith(base, "http://")) return strlen(base) > 7;
#endif
    return false;
}

Response request(const char* method, const char* base, const char* path, const char* body, const char* secret) {
    Response r;
    if (!urlAllowed(base)) {
        r.error = Error::BadUrl;
        return r;
    }
    const String url = joinUrl(base, path);
    log_i("HTTP %s %s", method, url.c_str());  // URL only: no secret, no body
    HTTPClient http;
    if (startsWith(base, "https://")) {
        WiFiClientSecure tls;
        tls.setCACert(TELOS_ROOT_CERTS);
        tls.setTimeout(TIMEOUT_MS / 1000);
        if (!http.begin(tls, url)) {
            r.error = Error::BadUrl;
            return r;
        }
        r = send(http, &tls, method, body, secret);
    } else {
        WiFiClient plain;
        if (!http.begin(plain, url)) {
            r.error = Error::BadUrl;
            return r;
        }
        r = send(http, nullptr, method, body, secret);
    }
    if (r.status) log_i("HTTP %s %s -> %d", method, path, r.status);
    return r;
}

const char* name(Error e) {
    switch (e) {
        case Error::None: return "-";
        case Error::Unreachable: return "SERVER_UNREACHABLE";
        case Error::Tls: return "TLS_ERROR";
        case Error::BadUrl: return "INVALID_PAYLOAD";
    }
    return "-";
}

}  // namespace https
