#include "debug/console.h"

#include <Arduino.h>
#include <string.h>

namespace console {
namespace {

constexpr int MAX_COMMANDS = 16;
constexpr size_t MAX_LINE = 128;

struct Command {
    const char* name;
    const char* help;
    Handler handler;
};

Command commands[MAX_COMMANDS];
int count = 0;
char line[MAX_LINE];
size_t length = 0;
Handler secretHandler = nullptr;

void printHelp() {
    Serial.println("commands:");
    for (int i = 0; i < count; i++) Serial.printf("  %-8s %s\n", commands[i].name, commands[i].help);
}

void run() {
    line[length] = '\0';
    if (secretHandler) {
        Handler h = secretHandler;
        secretHandler = nullptr;
        h(line);
        memset(line, 0, sizeof(line));
        length = 0;
        return;
    }
    char* cmd = line;
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return;
    char* args = strchr(cmd, ' ');
    if (args) {
        *args++ = '\0';
        while (*args == ' ') args++;
    } else {
        args = cmd + strlen(cmd);
    }
    if (strcmp(cmd, "help") == 0) {
        printHelp();
        return;
    }
    for (int i = 0; i < count; i++) {
        if (strcmp(cmd, commands[i].name) == 0) {
            commands[i].handler(args);
            return;
        }
    }
    Serial.printf("unknown command '%s', type help\n", cmd);
}

}  // namespace

void add(const char* name, const char* help, Handler handler) {
    if (count < MAX_COMMANDS) commands[count++] = {name, help, handler};
}

void readSecret(const char* prompt, Handler onLine) {
    secretHandler = onLine;
    Serial.print(prompt);
}

void poll() {
    while (Serial.available()) {
        const char c = char(Serial.read());
        if (c == '\r' || c == '\n') {
            if (length == 0 && !secretHandler) continue;  // \r\n pairs
            Serial.println();
            run();
            length = 0;
        } else if (c == '\b' || c == 0x7F) {
            // Remove a whole UTF-8 character (continuation bytes are 10xxxxxx), e.g. Cyrillic.
            while (length > 0 && (uint8_t(line[length - 1]) & 0xC0) == 0x80) length--;
            if (length > 0) {
                length--;
                Serial.print("\b \b");
            }
        } else if (length + 1 < MAX_LINE && uint8_t(c) >= 0x20) {
            line[length++] = c;
            Serial.print(secretHandler ? '*' : c);
        }
    }
}

}  // namespace console
