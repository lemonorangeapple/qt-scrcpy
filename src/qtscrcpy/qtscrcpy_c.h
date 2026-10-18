#ifndef QTSCRCPY_C_H
#define QTSCRCPY_C_H

/*
 * Plain C interface of qtscrcpy.dll: list devices reachable through adb and hdc, and mirror a device's
 * screen or camera, whichever tool reaches it. No Qt types appear here, so any language that can call
 * a C DLL can use it (C, C#, Python ctypes, Delphi, ...). Calls are thread-safe.
 *
 * Strings are UTF-8. Functions returning `const char *` give a pointer valid until the next call of the
 * same function on the same thread; copy it if you need it longer.
 *
 * Typical use:
 *     qsc_init(NULL);
 *     qsc_device d[16]; int n = qsc_list_devices(d, 16);
 *     qsc_session *s = qsc_session_create(d[0].backend, d[0].serial, QSC_SOURCE_SCREEN);
 *     qsc_session_start(s, on_frame, on_finished, user);
 *     ... frames arrive on a library thread ...
 *     qsc_session_stop(s); qsc_session_destroy(s);
 *     qsc_shutdown();
 */

#include <stdint.h>

#if defined(_WIN32)
#  define QSC_CALL __cdecl
#  if defined(QTSCRCPY_C_BUILD)
#    if defined(QTSCRCPY_C_STATIC)
#      define QSC_API
#    else
#      define QSC_API __declspec(dllexport)
#    endif
#  elif defined(QTSCRCPY_C_STATIC)
#    define QSC_API
#  else
#    define QSC_API __declspec(dllimport)
#  endif
#else
#  define QSC_API __attribute__((visibility("default")))
#  define QSC_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define QSC_VERSION_MAJOR 1
#define QSC_VERSION_MINOR 0

typedef enum qsc_backend {
    QSC_BACKEND_ADB = 0, /* Android; HarmonyOS with the Android layer */
    QSC_BACKEND_HDC = 1  /* HarmonyOS NEXT / OpenHarmony (needs the streamer app from harmony/) */
} qsc_backend;

typedef enum qsc_source {
    QSC_SOURCE_SCREEN = 0,
    QSC_SOURCE_CAMERA_BACK = 1,
    QSC_SOURCE_CAMERA_FRONT = 2,
    QSC_SOURCE_CAMERA_EXTERNAL = 3 /* adb only */
} qsc_source;

typedef enum qsc_device_state {
    QSC_DEVICE_OFFLINE = 0,
    QSC_DEVICE_ONLINE = 1,       /* ready: can be mirrored */
    QSC_DEVICE_UNAUTHORIZED = 2  /* waiting for "Allow USB debugging" on the phone */
} qsc_device_state;

typedef enum qsc_session_status {
    QSC_SESSION_STOPPED = 0,
    QSC_SESSION_STARTING = 1,
    QSC_SESSION_STREAMING = 2
} qsc_session_status;

typedef struct qsc_device {
    int backend;        /* qsc_backend */
    int state;          /* qsc_device_state */
    char serial[128];   /* pass to qsc_session_create */
    char name[128];     /* model name, or the serial when unknown */
} qsc_device;

/* One decoded picture. `bgra` is width*height*4 bytes (B,G,R,A per pixel, rows top to bottom, no padding)
 * and is only valid during the callback. */
typedef struct qsc_frame {
    int width;
    int height;
    const uint8_t *bgra;
} qsc_frame;

typedef struct qsc_session qsc_session;

/* Called on a library thread, one call per picture. Keep it short: pictures are dropped while it runs. */
typedef void (QSC_CALL *qsc_frame_callback)(const qsc_frame *frame, void *user);
/* Called once when a session ends. `error` is NULL after qsc_session_stop(), else a readable reason. */
typedef void (QSC_CALL *qsc_finished_callback)(const char *error, void *user);

/* Starts the library's worker thread. `directory` is where adb, scrcpy-server and hdc are looked for first
 * (NULL = the folder of the executable). Returns 0 on success; may be called again (it counts). */
QSC_API int QSC_CALL qsc_init(const char *directory);
/* Stops every session and the worker thread (balances qsc_init). */
QSC_API void QSC_CALL qsc_shutdown(void);

/* Version of this interface, as major * 100 + minor. */
QSC_API int QSC_CALL qsc_interface_version(void);

/* 1 when video can be decoded (the library was built with FFmpeg), else 0. */
QSC_API int QSC_CALL qsc_can_decode(void);
/* 1 when the tool for `backend` (adb or hdc) was found, else 0. */
QSC_API int QSC_CALL qsc_backend_available(int backend);

/* Devices currently known, both tools. Fills at most `capacity` entries and returns how many exist (it may
 * exceed `capacity`). The first call starts watching the tools; give it a moment (~1 s) to find devices. */
QSC_API int QSC_CALL qsc_list_devices(qsc_device *out, int capacity);

/* NULL if the combination is possible, else why not (e.g. an external camera over hdc). */
QSC_API const char *QSC_CALL qsc_source_unsupported(int backend, int source);

/* Creates a session; it does nothing until qsc_session_start. Returns NULL on bad arguments. */
QSC_API qsc_session *QSC_CALL qsc_session_create(int backend, const char *serial, int source);
/* Optional limits (Android only, 0 = default); call before start. */
QSC_API void QSC_CALL qsc_session_set_limits(qsc_session *s, int max_size, int bit_rate, int max_fps);
/* Returns 0 if it is starting, else -1 (already running, or bad arguments). */
QSC_API int QSC_CALL qsc_session_start(qsc_session *s, qsc_frame_callback on_frame,
                                       qsc_finished_callback on_finished, void *user);
/* Asks the session to end; `on_finished` follows. Does not wait. */
QSC_API void QSC_CALL qsc_session_stop(qsc_session *s);
QSC_API int QSC_CALL qsc_session_state(const qsc_session *s); /* a qsc_session_status */
/* Stops the session, waits until no callback can run any more, and frees it. Do not call it from a callback. */
QSC_API void QSC_CALL qsc_session_destroy(qsc_session *s);

#ifdef __cplusplus
}
#endif

#endif /* QTSCRCPY_C_H */
