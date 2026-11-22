#include "ui/screens.h"

#include <Arduino.h>
#include <esp_system.h>
#include <string.h>

#include "core/device.h"
#include "ui/display.h"
#include "ui/rgb_led.h"

namespace screens {
namespace {

using timer::Mode;
using timer::Status;

struct Drawn {
    bool valid = false;
    Screen screen = Screen::Main;
    Mode mode = Mode::Work;
    Status status = Status::Stopped;
    uint32_t remaining = UINT32_MAX;
    faces::Face face = faces::Face::Unknown;
    uint32_t uptimeS = UINT32_MAX;
    bool toast = false;
};

Drawn drawn;
char drawnProvisioning[64] = "";

TFT_eSPI& t() {
    return display::tft();
}

uint16_t modeColor(Mode m) {
    const rgb::Color c = m == Mode::Work ? rgb::WORK : (m == Mode::ShortBreak ? rgb::SHORT_BREAK : rgb::LONG_BREAK);
    return t().color565(c.r, c.g, c.b);
}

const char* modeTitle(Mode m) {
    switch (m) {
        case Mode::Work: return "FOCUS";
        case Mode::ShortBreak: return "SHORT BREAK";
        case Mode::LongBreak: return "LONG BREAK";
    }
    return "";
}

const char* statusText(Status s) {
    switch (s) {
        case Status::Running: return "running";
        case Status::Paused: return "paused";
        case Status::Stopped: return "ready";
        case Status::Finished: return "done";
    }
    return "";
}

void mmss(char* buf, size_t len, uint32_t seconds) {
    snprintf(buf, len, "%02lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
}

void text(const char* s, int x, int y, int font, uint16_t color, uint8_t datum = MC_DATUM, int pad = 0) {
    t().setTextDatum(datum);
    t().setTextColor(color, TFT_BLACK);
    t().setTextPadding(pad);
    t().drawString(s, x, y, font);
    t().setTextPadding(0);
}

void bottomLine(const char* s, uint16_t color) {
    text(s, 120, 222, 2, color, MC_DATUM, 236);
}

// --- Main -----------------------------------------------------------------

void mainStatic(const View& v) {
    const timer::State& st = *v.state;
    t().fillScreen(TFT_BLACK);
    t().fillRect(0, 0, 240, 34, modeColor(st.mode));
    t().setTextColor(TFT_BLACK, modeColor(st.mode));
    t().setTextDatum(MC_DATUM);
    t().drawString(modeTitle(st.mode), 120, 18, 4);

    if (st.status == Status::Finished) {
        text(st.mode == Mode::Work ? "Focus done" : "Break done", 120, 95, 4, TFT_WHITE);
        char buf[40];
        snprintf(buf, sizeof(buf), "next: %s", modeTitle(timer::localNextMode(st.mode)));
        text(buf, 120, 140, 4, modeColor(timer::localNextMode(st.mode)));
        if (!v.toast) bottomLine("flip, or red button to start", TFT_LIGHTGREY);
        return;
    }
    text(statusText(st.status), 120, 50, 2, st.status == Status::Paused ? TFT_ORANGE : TFT_LIGHTGREY);
    t().drawRect(19, 168, 202, 12, TFT_DARKGREY);
    if (!v.toast) {
        const char* hint = st.status == Status::Stopped  ? "flip the cube to start"
                           : st.status == Status::Paused ? "flip back to continue"
                                                         : "PAUSE face = pause";
        bottomLine(hint, TFT_DARKGREY);
    }
}

void mainDynamic(const View& v) {
    const timer::State& st = *v.state;
    if (st.status == Status::Finished) return;
    char buf[8];
    mmss(buf, sizeof(buf), v.remainingSeconds);
    const uint16_t color = st.status == Status::Running ? TFT_WHITE : TFT_LIGHTGREY;
    text(buf, 120, 112, 7, color, MC_DATUM, t().textWidth("88:88", 7));

    const uint32_t done = st.durationSeconds > v.remainingSeconds ? st.durationSeconds - v.remainingSeconds : 0;
    const int w = st.durationSeconds ? int(200 * done / st.durationSeconds) : 0;
    t().fillRect(20, 169, 200, 10, TFT_BLACK);
    if (w > 0) t().fillRect(20, 169, w, 10, modeColor(st.mode));
}

// --- Info (DISPLAY face) --------------------------------------------------

void infoStatic(const View& v) {
    t().fillScreen(TFT_BLACK);
    text("DISPLAY", 120, 14, 2, TFT_DARKGREY);
    text(modeTitle(v.state->mode), 120, 42, 4, modeColor(v.state->mode));
    text(statusText(v.state->status), 120, 68, 2, TFT_LIGHTGREY);
    t().setTextColor(TFT_DARKGREY, TFT_BLACK);
    t().setTextDatum(TL_DATUM);
    t().drawString("left", 12, 92, 2);
    t().drawString("phase", 12, 132, 2);
    t().drawString("next", 12, 152, 2);
    t().drawString("session", 12, 172, 2);
    t().drawString("ends at", 12, 192, 2);
    if (!v.toast) bottomLine("flip to a mode face to go on", TFT_DARKGREY);
}

void infoDynamic(const View& v) {
    const timer::State& st = *v.state;
    char buf[48];
    mmss(buf, sizeof(buf), v.remainingSeconds);
    text(buf, 228, 100, 4, TFT_WHITE, MR_DATUM, 150);
    mmss(buf, sizeof(buf), st.durationSeconds);
    text(buf, 228, 140, 2, TFT_LIGHTGREY, MR_DATUM, 150);
    text(modeTitle(timer::localNextMode(st.mode)), 228, 160, 2, TFT_LIGHTGREY, MR_DATUM, 150);
    snprintf(buf, sizeof(buf), "%.8s", st.sessionId[0] ? st.sessionId : "-");
    text(buf, 228, 180, 2, TFT_LIGHTGREY, MR_DATUM, 150);
    text(v.endsAt && v.endsAt[0] ? v.endsAt : "-", 228, 200, 2, TFT_LIGHTGREY, MR_DATUM, 150);
}

// --- Tech (TECH face) -----------------------------------------------------

void techStatic(const View& v) {
    t().fillScreen(TFT_BLACK);
    t().fillRect(0, 0, 240, 22, TFT_NAVY);
    t().setTextColor(TFT_WHITE, TFT_NAVY);
    t().setTextDatum(MC_DATUM);
    t().drawString("TECH", 120, 11, 2);
    t().setTextColor(TFT_DARKGREY, TFT_BLACK);
    t().setTextDatum(TL_DATUM);
    const char* labels[] = {"firmware", "hardware id", "uptime", "free heap", "events", "Wi-Fi", "time", "sensor"};
    for (int i = 0; i < 8; i++) t().drawString(labels[i], 8, 28 + i * 22, 2);
    if (!v.toast) bottomLine("blue 3 s: Bluetooth, 10 s: reset", TFT_DARKGREY);
}

void techDynamic(const View& v) {
    char buf[32];
    const uint32_t up = uint32_t(device::monotonicMs() / 1000);
    const int x = 232;
    auto y = [](int row) { return 36 + row * 22; };
    text(FIRMWARE_VERSION, x, y(0), 2, TFT_WHITE, MR_DATUM, 120);
    text(device::hardwareId(), x, y(1), 2, TFT_WHITE, MR_DATUM, 120);
    snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60),
             (unsigned long)(up % 60));
    text(buf, x, y(2), 2, TFT_WHITE, MR_DATUM, 120);
    snprintf(buf, sizeof(buf), "%lu B", (unsigned long)esp_get_free_heap_size());
    text(buf, x, y(3), 2, TFT_WHITE, MR_DATUM, 120);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)v.eventsThisBoot);
    text(buf, x, y(4), 2, TFT_WHITE, MR_DATUM, 120);
    text(v.wifiLine, x, y(5), 2, v.wifiOk ? TFT_GREEN : TFT_ORANGE, MR_DATUM, 140);
    text(v.timeLine, x, y(6), 2, TFT_WHITE, MR_DATUM, 120);
    text(v.sensorOk ? "MPU6050 ok" : "MPU6050 missing", x, y(7), 2, v.sensorOk ? TFT_GREEN : TFT_RED, MR_DATUM, 120);
}

// --- Provisioning (no device secret yet, or blue button held 3 s) ------------

void provisioningStatic(const View& v) {
    t().fillScreen(TFT_BLACK);
    t().fillRect(0, 0, 240, 34, TFT_BLUE);
    t().setTextColor(TFT_WHITE, TFT_BLUE);
    t().setTextDatum(MC_DATUM);
    t().drawString("ADD THIS CUBE", 120, 18, 4);
    text(v.bleName ? v.bleName : "", 120, 62, 4, TFT_WHITE);
    text("On teclos.space open", 120, 100, 2, TFT_LIGHTGREY);
    text("Devices > Add device", 120, 120, 2, TFT_WHITE);
    text("and choose this cube", 120, 140, 2, TFT_LIGHTGREY);
    if (!v.toast) bottomLine("blue 3 s: cancel (paired cube)", TFT_DARKGREY);
}

void provisioningDynamic(const View& v) {
    const char* st = v.provisioningStatus && v.provisioningStatus[0] ? v.provisioningStatus : "IDLE";
    const bool failed = strstr(st, "FAILED") != nullptr || strstr(st, "INVALID") != nullptr;
    const bool done = strcmp(st, "PAIRED") == 0;
    text(st, 120, 180, 2, failed ? TFT_RED : (done ? TFT_GREEN : TFT_YELLOW), MC_DATUM, 236);
}

}  // namespace

void begin() {
    display::begin();
    drawn = Drawn{};
}

void invalidate() {
    drawn.valid = false;
}

namespace {
// Rotation per board orientation (+X, -X, +Y, -Y, +Z, -Z); -1 = keep the current one.
// Opposite tilts differ by 180 deg, neighbouring tilts by 90 deg.
constexpr int8_t DEFAULT_ROTATION[6] = {2, 0, 1, 3, -1, -1};  // breadboard rig
int8_t rotationFor[6] = {2, 0, 1, 3, -1, -1};
uint8_t currentRotation = 0;
}  // namespace

void followOrientation(faces::Orientation o) {
    if (o == faces::Orientation::None) return;
    const int8_t r = rotationFor[static_cast<int>(o)];
    if (r < 0 || uint8_t(r) == currentRotation) return;
    currentRotation = uint8_t(r);
    t().setRotation(currentRotation);
    invalidate();
}

uint8_t rotation() {
    return currentRotation;
}

void setRotationTable(const int8_t table[6]) {
    for (int i = 0; i < 6; i++) rotationFor[i] = table[i] < -1 || table[i] > 3 ? -1 : table[i];
}

void getRotationTable(int8_t out[6]) {
    for (int i = 0; i < 6; i++) out[i] = rotationFor[i];
}

void defaultRotationTable(int8_t out[6]) {
    for (int i = 0; i < 6; i++) out[i] = DEFAULT_ROTATION[i];
}

Screen forFace(faces::Face face) {
    if (face == faces::Face::Display) return Screen::Info;
    if (face == faces::Face::Tech) return Screen::Tech;
    return Screen::Main;
}

void render(const View& v) {
    const timer::State& st = *v.state;
    const bool toast = v.toast != nullptr;
    const bool full = !drawn.valid || drawn.screen != v.screen || drawn.mode != st.mode ||
                      drawn.status != st.status || drawn.toast != toast;
    const uint32_t up = uint32_t(device::monotonicMs() / 1000);

    if (full) {
        switch (v.screen) {
            case Screen::Main: mainStatic(v); break;
            case Screen::Info: infoStatic(v); break;
            case Screen::Tech: techStatic(v); break;
            case Screen::Provisioning: provisioningStatic(v); break;
        }
        if (toast) bottomLine(v.toast, TFT_YELLOW);
    }
    const bool provisioningChanged = v.screen == Screen::Provisioning && v.provisioningStatus &&
                                     strcmp(drawnProvisioning, v.provisioningStatus) != 0;
    if (provisioningChanged) snprintf(drawnProvisioning, sizeof(drawnProvisioning), "%s", v.provisioningStatus);
    const bool tickChanged = full || provisioningChanged || drawn.remaining != v.remainingSeconds ||
                             (v.screen == Screen::Tech && drawn.uptimeS != up);
    if (tickChanged) {
        switch (v.screen) {
            case Screen::Main: mainDynamic(v); break;
            case Screen::Info: infoDynamic(v); break;
            case Screen::Tech: techDynamic(v); break;
            case Screen::Provisioning: provisioningDynamic(v); break;
        }
    }
    drawn.valid = true;
    drawn.screen = v.screen;
    drawn.mode = st.mode;
    drawn.status = st.status;
    drawn.remaining = v.remainingSeconds;
    drawn.face = v.face;
    drawn.uptimeS = up;
    drawn.toast = toast;
}

}  // namespace screens
