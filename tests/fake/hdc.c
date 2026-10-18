/* Fake hdc used by the tests. Behaviour is chosen with environment variables:
 *
 *   FAKE_HDC_LOG       file that receives one line per call (argv joined by spaces)
 *   FAKE_HDC_DUMP      "present" -> `bm dump -n <bundle>` answers with the bundle name, else it fails
 *   FAKE_HDC_INSTALL   "ok" (default) or "fail": what `install -r` answers
 *   FAKE_HDC_UDID      the UDID `bm get -u` prints (default: a fixed 64 hex digit one)
 *   FAKE_HDC_RECV      file that `file recv` hands over (a real JPEG for the screenshot fallback)
 *   FAKE_HDC_FPORT     "ok" (default) or "fail"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int equals(const char *value, const char *expected)
{
    return value && strcmp(value, expected) == 0;
}

static void copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in)
        return;
    FILE *out = fopen(to, "wb");
    if (out) {
        char buffer[8192];
        size_t n;
        while ((n = fread(buffer, 1, sizeof buffer, in)) > 0)
            fwrite(buffer, 1, n, out);
        fclose(out);
    }
    fclose(in);
}

int main(int argc, char **argv)
{
    const char *path = getenv("FAKE_HDC_LOG");
    if (path) {
        FILE *log = fopen(path, "a");
        if (log) {
            for (int i = 1; i < argc; i++)
                fprintf(log, "%s%s", argv[i], i + 1 < argc ? " " : "\n");
            fclose(log);
        }
    }

    if (argc >= 3 && strcmp(argv[1], "list") == 0) {
        printf("FAKEUDID0001\tUSB\tConnected\tlocalhost\thdc\r\n");
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "dump") == 0) {
            if (equals(getenv("FAKE_HDC_DUMP"), "present")) {
                printf("{\"name\": \"com.qtscrcpy.streamer\"}\n");
                return 0;
            }
            printf("error: the bundle does not exist\n");
            return 1;
        }
        if (strcmp(argv[i], "get") == 0) {
            const char *udid = getenv("FAKE_HDC_UDID");
            printf("udid of current device is : %s\n",
                   udid ? udid : "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF");
            return 0;
        }
        if (strcmp(argv[i], "install") == 0) {
            if (equals(getenv("FAKE_HDC_INSTALL"), "fail")) {
                printf("[Fail]error: signature verification failed\n");
                return 1;
            }
            printf("install bundle successfully.\n");
            return 0;
        }
        if (strcmp(argv[i], "snapshot_display") == 0) {
            return 0; /* the "device" writes its picture when it is fetched */
        }
        if (strcmp(argv[i], "recv") == 0 && i + 2 < argc) {
            const char *source = getenv("FAKE_HDC_RECV");
            if (!source)
                return 1;
            copy_file(source, argv[argc - 1]);
            return 0;
        }
        if (strcmp(argv[i], "fport") == 0) {
            if (equals(getenv("FAKE_HDC_FPORT"), "fail")) {
                printf("[Fail]no device\n");
                return 1;
            }
            printf("Forwardport result:OK\n");
            return 0;
        }
        if (strcmp(argv[i], "aa") == 0) {
            printf("start ability successfully.\n");
            return 0;
        }
    }
    return 0;
}
