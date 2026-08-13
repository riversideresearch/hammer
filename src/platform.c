#define _GNU_SOURCE // to obtain asprintf/vasprintf
#include "platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(_WIN32)
#include <io.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

int h_platform_asprintf(char **strp, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int res = h_platform_vasprintf(strp, fmt, ap);
    va_end(ap);
    return res;
}

int h_platform_vasprintf(char **strp, const char *fmt, va_list arg) {
    return vasprintf(strp, fmt, arg);
}

int h_platform_is_terminal(FILE *stream) {
    if (!stream)
        return 0;
#if defined(_WIN32)
    return _isatty(_fileno(stream));
#elif defined(__unix__) || defined(__APPLE__)
    return isatty(fileno(stream));
#else
    return 0;
#endif
}

void h_platform_errx(int err, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(err);
}

// TODO: replace this with a posix timer-based benchmark. (cf. timerfd_create, timer_create,
// setitimer)

static void gettime(struct timespec *ts) {
    if (ts == NULL)
        return;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, ts);
}

void h_platform_stopwatch_reset(struct HStopWatch *stopwatch) { gettime(&stopwatch->start); }

int64_t h_platform_stopwatch_ns(struct HStopWatch *stopwatch) {
    struct timespec ts_now;
    gettime(&ts_now);

    // time_diff is in ns
    return (ts_now.tv_sec - stopwatch->start.tv_sec) * 1000000000 +
           (ts_now.tv_nsec - stopwatch->start.tv_nsec);
}
