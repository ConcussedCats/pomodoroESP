#pragma once

// Serial command line for debug builds (TELOS_DEBUG). Type a command in
// `pio device monitor` and press Enter. Never prints secrets: an input line can be
// marked secret, then it is echoed as '*'.
namespace console {

using Handler = void (*)(const char* args);

void add(const char* name, const char* help, Handler handler);
void poll();  // call from the loop

// The next line is read as a secret (echo '*'), handed to `onLine`, then wiped.
void readSecret(const char* prompt, Handler onLine);

}  // namespace console
