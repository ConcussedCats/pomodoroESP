#include "core/device.h"

#include <esp_mac.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <stdio.h>

namespace device {

const char* hardwareId() {
    static char id[13] = "";
    if (id[0] == '\0') {
        uint8_t mac[6] = {0};
        esp_efuse_mac_get_default(mac);
        snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    return id;
}

void newUuid(char out[37]) {
    uint8_t b[16];
    esp_fill_random(b, sizeof(b));
    b[6] = (b[6] & 0x0F) | 0x40;  // version 4
    b[8] = (b[8] & 0x3F) | 0x80;  // RFC 4122 variant
    snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2],
             b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

int64_t monotonicMs() {
    return esp_timer_get_time() / 1000;
}

}  // namespace device
