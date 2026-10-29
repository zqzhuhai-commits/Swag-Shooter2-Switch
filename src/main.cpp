#include "probe.h"
#include <exception>
#ifdef __SWITCH__
#include <switch.h>
#endif

int main(int argc, char** argv) {
#ifdef __SWITCH__
    consoleInit(nullptr);
    std::puts("SWAG2 0.0.1 - ARMv7 execution test\nNot a playable game build.\n");
    std::puts("Starting JIT tests. Use full-memory hbmenu (title override).\n");
    consoleUpdate(nullptr);
    const char* path = "sdmc:/switch/swag2-test/libil2cpp.so";
    FILE* log = std::fopen("sdmc:/switch/swag2-armv7-test.log", "w");
#else
    const char* path = argc > 1 ? argv[1] : "libil2cpp.so";
    FILE* log = stdout;
#endif
    (void)argc; (void)argv;
    if (!log) log = stdout;
    std::setvbuf(log, nullptr, _IONBF, 0);
    int result = 1;
    try { result = run_probe(log, path); }
    catch (const std::exception& error) {
        std::fprintf(log, "HOST_EXCEPTION %s\n", error.what());
    }
    std::fprintf(log, "PROBE_RESULT=%s UNITY_STARTED=NO GAMEPLAY=NOT_IMPLEMENTED\n", result ? "FAIL_OR_BLOCKED" : "PASS");
#ifdef __SWITCH__
    std::printf("\nResult: %s\nLog: /switch/swag2-armv7-test.log\nUnity/gameplay are not started.\nPress + to exit.\n", result ? "FAIL / BLOCKED" : "ARMv7 TEST PASS");
    PadState pad; padConfigureInput(1, HidNpadStyleSet_NpadStandard); padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(nullptr);
    }
    if (log != stdout) std::fclose(log);
    consoleExit(nullptr);
#endif
    return result;
}
