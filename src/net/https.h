#pragma once
#include <Arduino.h>

// HTTPS JSON client for the Telos API (FIRMWARE-SPEC 3.5, PROTOCOL.md 2).
// - certificate is validated against the embedded Amazon roots (never setInsecure)
// - redirects are not followed (the bare domain answers POST with a redirect / 405)
// - http:// only in debug builds (TELOS_DEBUG), for a local server
// - the device secret is sent in a header and never logged
namespace https {

enum class Error : uint8_t { None, Unreachable, Tls, BadUrl };

struct Response {
    int status = 0;  // HTTP status, 0 when the request did not reach the server
    Error error = Error::None;
    String body;
};

bool urlAllowed(const char* baseUrl);

// method "GET" / "POST"; body may be nullptr; secret nullptr = no device auth (pairing).
Response request(const char* method, const char* baseUrl, const char* path, const char* body, const char* secret);

const char* name(Error e);  // "SERVER_UNREACHABLE", "TLS_ERROR", ...

}  // namespace https
