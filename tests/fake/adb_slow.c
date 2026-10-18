/* Fake adb for the slow-server check: `version` and `start-server` answer at once, `devices` hangs.
   environment: FAKE_ADB_LOG (one line per call), FAKE_ADB_HANG_MS (default 10000) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

int main(int argc, char **argv)
{
    const char *log = getenv("FAKE_ADB_LOG");
    if (log) {
        FILE *f = fopen(log, "a");
        if (f) {
            for (int i = 1; i < argc; i++)
                fprintf(f, "%s%s", argv[i], i + 1 < argc ? " " : "\n");
            fclose(f);
        }
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "version") == 0) {
            printf("Android Debug Bridge version 1.0.41\nVersion 36.0.0-fake\n");
            return 0;
        }
        if (strcmp(argv[i], "start-server") == 0)
            return 0;
        if (strcmp(argv[i], "track-devices") == 0) {
            Sleep(60000);
            return 0;
        }
        if (strcmp(argv[i], "devices") == 0) {
            const char *ms = getenv("FAKE_ADB_HANG_MS");
            Sleep((DWORD)(ms ? atoi(ms) : 10000));
            printf("List of devices attached\n\n");
            return 0;
        }
    }
    return 0;
}
