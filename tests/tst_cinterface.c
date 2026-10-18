/* Uses nothing but the C header: the point is that a plain C program can drive qtscrcpy.dll. */
#include <qtscrcpy/qtscrcpy_c.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  define SLEEP_MS(ms) Sleep(ms)
#else
#  include <unistd.h>
#  define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static volatile int g_frames = 0;
static volatile int g_width = 0, g_height = 0, g_nonzero = 0, g_finished = 0;
static char g_error[512];

static void QSC_CALL on_frame(const qsc_frame *frame, void *user)
{
    (void)user;
    g_width = frame->width;
    g_height = frame->height;
    g_nonzero = 0;
    for (int i = 0; i < frame->width * frame->height * 4; i += 997)
        if (frame->bgra[i]) g_nonzero = 1;
    ++g_frames;
}

static void QSC_CALL on_finished(const char *error, void *user)
{
    (void)user;
    g_error[0] = '\0';
    if (error) { strncpy(g_error, error, sizeof g_error - 1); g_error[sizeof g_error - 1] = '\0'; }
    g_finished = 1;
}

static int wait_for(volatile int *flag, int target, int timeout_ms)
{
    for (int waited = 0; waited < timeout_ms; waited += 50) {
        if (*flag >= target) return 1;
        SLEEP_MS(50);
    }
    return *flag >= target;
}

int main(void)
{
    CHECK(qsc_interface_version() == 100, "interface version %d", qsc_interface_version());
    CHECK(qsc_can_decode() == 1, "built without a decoder");

    /* Before init everything is a harmless no-op. */
    CHECK(qsc_session_create(QSC_BACKEND_ADB, "SER", QSC_SOURCE_SCREEN) == NULL, "session before init");
    CHECK(qsc_list_devices(NULL, 0) == 0, "list before init");

    CHECK(qsc_init(NULL) == 0, "init");
    CHECK(qsc_init(NULL) == 0, "second init counts");

    CHECK(qsc_source_unsupported(QSC_BACKEND_HDC, QSC_SOURCE_CAMERA_EXTERNAL) != NULL, "hdc external camera must be refused");
    CHECK(qsc_source_unsupported(QSC_BACKEND_ADB, QSC_SOURCE_CAMERA_EXTERNAL) == NULL, "adb external camera is fine");
    CHECK(qsc_source_unsupported(99, 0) != NULL, "unknown backend");
    CHECK(qsc_source_unsupported(QSC_BACKEND_ADB, 77) != NULL, "unknown source");

    CHECK(qsc_session_create(QSC_BACKEND_ADB, NULL, QSC_SOURCE_SCREEN) == NULL, "NULL serial");
    CHECK(qsc_session_create(QSC_BACKEND_ADB, "", QSC_SOURCE_SCREEN) == NULL, "empty serial");
    CHECK(qsc_session_create(5, "SER", QSC_SOURCE_SCREEN) == NULL, "bad backend");

    /* An unsupported combination ends at once, with a reason. */
    {
        qsc_session *s = qsc_session_create(QSC_BACKEND_HDC, "X", QSC_SOURCE_CAMERA_EXTERNAL);
        CHECK(s != NULL, "create");
        g_finished = 0;
        CHECK(qsc_session_start(s, on_frame, on_finished, NULL) == 0, "start");
        CHECK(wait_for(&g_finished, 1, 3000), "no finish callback");
        CHECK(g_error[0] != '\0', "expected an error text");
        CHECK(qsc_session_state(s) == QSC_SESSION_STOPPED, "state after failure");
        qsc_session_destroy(s);
    }

    /* A real stream from the fake adb server (its port is in ANDROID_ADB_SERVER_PORT). */
    {
        qsc_session *s = qsc_session_create(QSC_BACKEND_ADB, "SER", QSC_SOURCE_SCREEN);
        CHECK(s != NULL, "create adb session");
        qsc_session_set_limits(s, 1280, 0, 0);
        g_frames = 0; g_finished = 0; g_error[0] = '\0';
        CHECK(qsc_session_start(s, on_frame, on_finished, NULL) == 0, "start adb session");
        CHECK(qsc_session_start(s, on_frame, on_finished, NULL) == -1, "second start must be refused");
        CHECK(wait_for(&g_frames, 8, 20000), "only %d frames", g_frames);
        CHECK(g_width == 160 && g_height == 96, "frame size %dx%d", g_width, g_height);
        CHECK(g_nonzero, "picture is all zero");
        CHECK(qsc_session_state(s) == QSC_SESSION_STREAMING, "state while streaming");
        qsc_session_stop(s);
        CHECK(wait_for(&g_finished, 1, 10000), "no finish after stop");
        CHECK(g_error[0] == '\0', "stop is not an error: %s", g_error);
        qsc_session_destroy(s);
    }

    /* Destroying a running session must not hang or crash. */
    {
        qsc_session *s = qsc_session_create(QSC_BACKEND_ADB, "SER", QSC_SOURCE_CAMERA_BACK);
        g_frames = 0;
        CHECK(qsc_session_start(s, on_frame, on_finished, NULL) == 0, "start camera session");
        CHECK(wait_for(&g_frames, 2, 20000), "camera frames");
        qsc_session_destroy(s);
    }

    /* Devices: the fake adb server has none, but listing must work and a small buffer must be safe. */
    {
        qsc_device list[2];
        int n = qsc_list_devices(list, 2);
        CHECK(n >= 0, "list devices");
        CHECK(qsc_backend_available(QSC_BACKEND_HDC) == 0 || qsc_backend_available(QSC_BACKEND_HDC) == 1, "backend flag");
    }

    qsc_shutdown();
    qsc_shutdown();
    CHECK(qsc_session_create(QSC_BACKEND_ADB, "SER", QSC_SOURCE_SCREEN) == NULL, "session after shutdown");

    if (failures) { printf("%d check(s) failed\n", failures); return 1; }
    printf("all checks passed\n");
    return 0;
}
