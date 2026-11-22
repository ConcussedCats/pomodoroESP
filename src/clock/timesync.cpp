#include "clock/timesync.h"

#include <Arduino.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

#include "net/wifi.h"

namespace timesync {
namespace {

// Display only; all protocol timestamps are UTC epoch ms.
constexpr const char* TZ_KYIV = "EET-2EEST,M3.5.0/3,M10.5.0/4";
constexpr time_t VALID_AFTER = 1700000000;  // 2023-11-14: anything earlier is the unset RTC

bool started = false;
bool isSynced = false;

}  // namespace

void begin() {
    setenv("TZ", TZ_KYIV, 1);
    tzset();
}

void loop() {
    if (!started && wifi::connected()) {
        started = true;
        configTzTime(TZ_KYIV, "pool.ntp.org", "time.google.com");
        log_i("SNTP started");
    }
    if (started && !isSynced) {
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED || time(nullptr) > VALID_AFTER) {
            isSynced = true;
            char buf[8];
            formatLocal(epochMs(), buf, sizeof(buf));
            log_i("clock synced, local time %s", buf);
        }
    }
}

bool synced() {
    return isSynced;
}

int64_t epochMs() {
    if (!isSynced) return -1;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return int64_t(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

void formatLocal(int64_t ms, char* out, size_t size) {
    if (size == 0) return;
    out[0] = '\0';
    if (ms < 0) return;
    const time_t t = time_t(ms / 1000);
    struct tm local;
    localtime_r(&t, &local);
    strftime(out, size, "%H:%M", &local);
}

}  // namespace timesync
