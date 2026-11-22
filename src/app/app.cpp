#include "app/app.h"

#include <Arduino.h>

#include "buttons/buttons.h"
#include "core/device.h"
#include "faces/faces.h"
#include "timer/timer_core.h"
#include "ui/buzzer.h"
#include "ui/rgb_led.h"
#include "ui/screens.h"
#include "ui/vibration.h"

// Phase lengths when no config was downloaded yet (env cube_fast shortens them for tests).
#ifndef TELOS_DEFAULT_WORK_S
#define TELOS_DEFAULT_WORK_S (25 * 60)
#endif
#ifndef TELOS_DEFAULT_SHORT_S
#define TELOS_DEFAULT_SHORT_S (5 * 60)
#endif
#ifndef TELOS_DEFAULT_LONG_S
#define TELOS_DEFAULT_LONG_S (15 * 60)
#endif

namespace app {
namespace {

using timer::EventType;
using timer::Mode;
using timer::Status;

constexpr uint32_t LOOP_MS = 5;
constexpr uint32_t TOAST_MS = 2500;
constexpr uint8_t DEFAULT_BRIGHTNESS = 60;

uint32_t eventsThisBoot = 0;
uint32_t lastFaceChanges = 0;

// Screen: follows the face (DISPLAY / TECH); the blue button switches manually until the next flip.
bool manualScreen = false;
screens::Screen screen = screens::Screen::Main;

const char* toast = nullptr;
uint32_t toastUntilMs = 0;

// Last (mode, status) the LED was set for, so effects are not restarted every loop.
bool ledValid = false;
Mode ledMode = Mode::Work;
Status ledStatus = Status::Stopped;

rgb::Color modeColor(Mode m) {
    return m == Mode::Work ? rgb::WORK : (m == Mode::ShortBreak ? rgb::SHORT_BREAK : rgb::LONG_BREAK);
}

void onTimerEvent(EventType type, faces::Face face);

timer::TimerCore core([] {
    timer::Env env;
    env.monotonicMs = [] { return device::monotonicMs(); };
    env.epochMs = [] { return timer::NONE; };  // SNTP comes in stage 3
    env.newUuid = [](char out[37]) { device::newUuid(out); };
    env.onEvent = [](EventType t, faces::Face f) { onTimerEvent(t, f); };
    return env;
}());

void showToast(const char* text, uint32_t now) {
    toast = text;
    toastUntilMs = now + TOAST_MS;
}

void onTimerEvent(EventType type, faces::Face face) {
    eventsThisBoot++;
    const timer::State& s = core.state();
    // Stage 6 turns this into the outbound event queue (bootId / seq / eventId).
    Serial.printf("[event] %-14s face=%-11s mode=%-11s status=%-8s remaining=%lu session=%s\n", timer::name(type),
                  type == EventType::FaceChanged ? faces::name(face) : "-", timer::name(s.mode), timer::name(s.status),
                  (unsigned long)core.remainingSeconds(), s.sessionId[0] ? s.sessionId : "null");

    switch (type) {
        case EventType::Started:
            vibration::phaseStart();
            s.mode == Mode::Work ? buzzer::playUp() : buzzer::playDown();
            break;
        case EventType::Resumed:
            vibration::ack();
            buzzer::beep(60);
            break;
        case EventType::Paused:
            vibration::ack();
            break;
        case EventType::Finished:
            vibration::phaseEnd();
            buzzer::playDown();
            break;
        default:
            break;
    }
}

void updateLed() {
    const timer::State& s = core.state();
    if (ledValid && s.mode == ledMode && s.status == ledStatus) return;
    ledValid = true;
    ledMode = s.mode;
    ledStatus = s.status;
    switch (s.status) {
        case Status::Running: rgb::show(modeColor(s.mode)); break;
        case Status::Paused: rgb::show(modeColor(s.mode), rgb::Effect::Breathe); break;
        case Status::Finished: rgb::show(modeColor(s.mode), rgb::Effect::Blink); break;
        case Status::Stopped: rgb::show(rgb::OFF); break;
    }
}

void onButton(const buttons::Event& e, uint32_t now) {
    using buttons::Gesture;
    using buttons::Id;
    Serial.printf("[button] %s %s\n", e.button == Id::S1 ? "blue(S1)" : "red(S2)", buttons::name(e.gesture));

    if (e.button == Id::S1) {
        switch (e.gesture) {
            case Gesture::Short:
                manualScreen = true;
                screen = screen == screens::Screen::Main   ? screens::Screen::Info
                         : screen == screens::Screen::Info ? screens::Screen::Tech
                                                           : screens::Screen::Main;
                break;
            case Gesture::HoldStart3s: buzzer::beep(60); break;
            case Gesture::Long: showToast("Bluetooth comes in stage 4", now); break;
            case Gesture::VeryLong: showToast("Factory reset comes in stage 9", now); break;
        }
        return;
    }
    // Red button: start the suggested next phase after FINISHED (like flipping to it).
    if (e.gesture != Gesture::Short) return;
    if (core.state().status == Status::Finished) {
        core.start(timer::localNextMode(core.state().mode));
    } else {
        showToast("red button: only after a phase is done", now);
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n=== Telos cube %s, hardware id %s, offline mode ===\n", FIRMWARE_VERSION, device::hardwareId());

    rgb::begin();
    rgb::setBrightness(DEFAULT_BRIGHTNESS);
    vibration::begin();
    buzzer::begin();
    buttons::begin();
    screens::begin();

    timer::Durations d;
    d.workSeconds = TELOS_DEFAULT_WORK_S;
    d.shortBreakSeconds = TELOS_DEFAULT_SHORT_S;
    d.longBreakSeconds = TELOS_DEFAULT_LONG_S;
    core.configure(d);
    Serial.printf("durations: work %lu s, short break %lu s, long break %lu s\n", (unsigned long)d.workSeconds,
                  (unsigned long)d.shortBreakSeconds, (unsigned long)d.longBreakSeconds);

    if (!faces::begin()) Serial.println("MPU6050 not found, faces disabled until it answers");
}

void loop() {
    const uint32_t now = millis();

    buttons::Event e;
    while (buttons::poll(now, e)) onButton(e, now);

    const faces::Reading r = faces::read();
    if (r.changes != lastFaceChanges) {
        lastFaceChanges = r.changes;
        manualScreen = false;
        core.applyFace(r.face);
    }
    core.tick();

    if (!manualScreen) screen = screens::forFace(core.face());
    if (toast && int32_t(now - toastUntilMs) >= 0) toast = nullptr;

    screens::View v{screen, &core.state(), core.remainingSeconds(), core.face(), r.sensorOk, eventsThisBoot, toast};
    screens::render(v);

    updateLed();
    rgb::update(now);
    vibration::update(now);
    buzzer::update(now);
    delay(LOOP_MS);
}

}  // namespace app
