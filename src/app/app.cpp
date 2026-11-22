#include "app/app.h"

#include <Arduino.h>

#include "buttons/buttons.h"
#include "core/device.h"
#include "faces/faces.h"
#include "storage/storage.h"
#include "timer/snapshot.h"
#include "timer/timer_core.h"
#include "ui/buzzer.h"
#include "ui/rgb_led.h"
#include "ui/screens.h"
#include "ui/vibration.h"

#ifdef TELOS_DEBUG
#include "debug/console.h"
#endif

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
// NVS save while counting down: at most this much is lost on power-off when there is no clock.
// ~8600 small writes a day, which the NVS wear levelling spreads over its pages.
constexpr uint32_t CHECKPOINT_MS = 10000;

uint32_t eventsThisBoot = 0;
uint32_t bootCounter = 0;
bool saveDue = false;
uint32_t lastSaveMs = 0;
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
    saveDue = true;  // written from the loop, not from inside the timer
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

void saveTimerIfDue(uint32_t now) {
    const bool checkpoint = core.state().status == Status::Running && now - lastSaveMs >= CHECKPOINT_MS;
    if (!saveDue && !checkpoint) return;
    storage::saveTimer(timer::capture(core));
    saveDue = false;
    lastSaveMs = now;
}

void loadCalibration() {
    faces::Face map[6];
    if (storage::loadFaceMap(map)) {
        faces::setMapping(map);
        Serial.println("face table: from NVS");
    } else {
        Serial.println("face table: default (breadboard rig)");
    }
    int8_t rot[6];
    if (storage::loadRotations(rot)) {
        screens::setRotationTable(rot);
        Serial.println("screen rotations: from NVS");
    }
}

void restoreTimer() {
    timer::Saved saved;
    if (!storage::loadTimer(saved)) {
        Serial.println("timer: nothing saved");
        return;
    }
    if (saved.status == Status::Stopped) {
        Serial.println("timer: saved idle, starting fresh");
        return;
    }
    Serial.printf("timer: restoring %s %s, %lu s left%s\n", timer::name(saved.mode), timer::name(saved.status),
                  (unsigned long)saved.remainingSeconds,
                  saved.status == Status::Running ? " (no clock: comes back paused)" : "");
    core.restoreAndSync(timer::restoredState(saved, timer::NONE));
}

#ifdef TELOS_DEBUG
const char* const AXES[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};

int axisIndex(const char* s) {
    for (int i = 0; i < 6; i++) {
        if (strncasecmp(s, AXES[i], 2) == 0) return i;
    }
    return -1;
}

bool parseFace(const char* s, faces::Face& out) {
    for (int i = 0; i <= static_cast<int>(faces::Face::Tech); i++) {
        const faces::Face f = static_cast<faces::Face>(i);
        if (strcasecmp(s, faces::name(f)) == 0) {
            out = f;
            return true;
        }
    }
    return false;
}

void printCalibration() {
    faces::Face map[6];
    int8_t rot[6];
    faces::getMapping(map);
    screens::getRotationTable(rot);
    Serial.println("axis  face         screen");
    for (int i = 0; i < 6; i++) {
        char r[8];
        if (rot[i] < 0) snprintf(r, sizeof(r), "keep");
        else snprintf(r, sizeof(r), "%d", rot[i] * 90);
        Serial.printf("  %s  %-11s  %s\n", AXES[i], faces::name(map[i]), r);
    }
}

void cmdStatus(const char*) {
    const timer::State& s = core.state();
    Serial.printf("boot #%lu, uptime %lu s\n", (unsigned long)bootCounter, (unsigned long)(device::monotonicMs() / 1000));
    Serial.printf("timer: %s %s, %lu s left, session %s\n", timer::name(s.mode), timer::name(s.status),
                  (unsigned long)core.remainingSeconds(), s.sessionId[0] ? s.sessionId : "null");
    printCalibration();
}

void cmdFace(const char* a) {
    const int i = axisIndex(a);
    faces::Face f;
    if (i < 0 || strlen(a) < 4 || !parseFace(a + 3, f) || f == faces::Face::Unknown) {
        Serial.println("usage: face <+X|-X|+Y|-Y|+Z|-Z> <WORK|SHORT_BREAK|LONG_BREAK|PAUSE|DISPLAY|TECH>");
        return;
    }
    faces::Face map[6];
    faces::getMapping(map);
    map[i] = f;
    faces::setMapping(map);
    storage::saveFaceMap(map);
    printCalibration();
}

void cmdRot(const char* a) {
    int i = axisIndex(a);
    const char* v = strlen(a) >= 4 ? a + 3 : "";
    int8_t r = 0;
    if (strcasecmp(v, "keep") == 0) {
        r = -1;
    } else if (strcmp(v, "0") == 0 || strcmp(v, "90") == 0 || strcmp(v, "180") == 0 || strcmp(v, "270") == 0) {
        r = int8_t(atoi(v) / 90);
    } else {
        i = -1;
    }
    if (i < 0) {
        Serial.println("usage: rot <+X|-X|+Y|-Y|+Z|-Z> <0|90|180|270|keep>");
        return;
    }
    int8_t rot[6];
    screens::getRotationTable(rot);
    rot[i] = r;
    screens::setRotationTable(rot);
    storage::saveRotations(rot);
    printCalibration();
}

void cmdCalib(const char* a) {
    if (strcmp(a, "reset") == 0) {
        storage::clearCalibration();
        faces::Face map[6];
        int8_t rot[6];
        faces::defaultMapping(map);
        screens::defaultRotationTable(rot);
        faces::setMapping(map);
        screens::setRotationTable(rot);
    }
    printCalibration();
}

void cmdReboot(const char*) {
    storage::saveTimer(timer::capture(core));
    Serial.println("rebooting...");
    Serial.flush();
    ESP.restart();
}

void setupConsole() {
    console::add("status", "boot counter, timer, calibration", cmdStatus);
    console::add("face", "face <axis> <FACE>, e.g. face -X WORK", cmdFace);
    console::add("rot", "rot <axis> <0|90|180|270|keep>", cmdRot);
    console::add("calib", "calib = show, calib reset = rig defaults", cmdCalib);
    console::add("reboot", "save the timer and restart", cmdReboot);
    Serial.println("debug console ready, type help");
}
#endif

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n=== Telos cube %s, hardware id %s, offline mode ===\n", FIRMWARE_VERSION, device::hardwareId());
    storage::begin();
    bootCounter = storage::bumpBootCounter();
    Serial.printf("boot #%lu\n", (unsigned long)bootCounter);

    rgb::begin();
    rgb::setBrightness(DEFAULT_BRIGHTNESS);
    vibration::begin();
    buzzer::begin();
    buttons::begin();
    screens::begin();
    loadCalibration();

    timer::Durations d;
    d.workSeconds = TELOS_DEFAULT_WORK_S;
    d.shortBreakSeconds = TELOS_DEFAULT_SHORT_S;
    d.longBreakSeconds = TELOS_DEFAULT_LONG_S;
    core.configure(d);
    Serial.printf("durations: work %lu s, short break %lu s, long break %lu s\n", (unsigned long)d.workSeconds,
                  (unsigned long)d.shortBreakSeconds, (unsigned long)d.longBreakSeconds);

    restoreTimer();
    if (!faces::begin()) Serial.println("MPU6050 not found, faces disabled until it answers");
#ifdef TELOS_DEBUG
    setupConsole();
#endif
}

void loop() {
    const uint32_t now = millis();
#ifdef TELOS_DEBUG
    console::poll();
#endif

    buttons::Event e;
    while (buttons::poll(now, e)) onButton(e, now);

    const faces::Reading r = faces::read();
    if (r.changes != lastFaceChanges) {
        lastFaceChanges = r.changes;
        manualScreen = false;
        screens::followOrientation(r.stable);
        Serial.printf("[face] axis %s -> %s, screen rotation %u\n", faces::name(r.stable), faces::name(r.face),
                      screens::rotation() * 90);
        core.applyFace(r.face);
    }
    core.tick();

    if (!manualScreen) screen = screens::forFace(core.face());
    if (toast && int32_t(now - toastUntilMs) >= 0) toast = nullptr;

    screens::View v{screen, &core.state(), core.remainingSeconds(), core.face(), r.sensorOk, eventsThisBoot, toast};
    screens::render(v);

    saveTimerIfDue(now);
    updateLed();
    rgb::update(now);
    vibration::update(now);
    buzzer::update(now);
    delay(LOOP_MS);
}

}  // namespace app
