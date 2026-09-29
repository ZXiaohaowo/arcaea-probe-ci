/* practice_clock_probe.c - S2a step 1: minimal sampling framework.
 *
 * Purpose: validate that a long-lived, low-frequency sampling thread can run
 * inside the game process harmlessly, and produce a structured diagnostic log
 * (build id, process session, monotonic time, sample sequence) that later S2
 * steps will extend with the read-only audio-position value.
 *
 * Step-1 scope: NO game object is read or called. libSystem only.
 *
 * Log: <sandbox>/Documents/practice_clock_probe.log (append-only; one header
 * line per app launch):
 *   # build=<id> pid=<n> utc=<ISO8601Z> t0_ms=<monotonic ms at module load>
 *   s seq=<n> t_ms=<monotonic ms> dt_ms=<ms since previous wake> [gap=1]
 *
 * Lifecycle policy (see docs/s2a_sampling_framework.md):
 *   - Game pause: not detectable without game access; the sampler runs
 *     continuously. Pause effects will be read FROM the audio-position
 *     behaviour in later steps.
 *   - Backgrounding: process suspension shows up as a large dt_ms marked
 *     gap=1; the monotonic clock is the only detector (no UIKit).
 *   - Exit/kill: no reliable termination hook on iOS; the 8-line buffer
 *     bounds possible sample loss to about 8 seconds (best effort).
 *
 * Safety rules honored here:
 *   - No game access; no heap allocation; no UIKit; no networking.
 *   - Any failure path is silent; this module must never abort the host app.
 */
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#ifndef PRACTICE_BUILD_ID
#define PRACTICE_BUILD_ID "dev"
#endif
#define PRACTICE_CLOCK_PROBE_VERSION "s2a-clock-" PRACTICE_BUILD_ID

#define SAMPLE_INTERVAL_MS 1000
#define GAP_FACTOR 3 /* dt >= GAP_FACTOR x interval counts as a gap */
#define FLUSH_EVERY_LINES 8
#define LINEBUF 160

__attribute__((visibility("default")))
const char practice_clock_probe_version[] = PRACTICE_CLOCK_PROBE_VERSION;

/* ---------------------------------------------------------------- path */

static char g_log_path[1024];
static int g_log_path_ok;

static void init_log_path(void)
{
    const char *home = getenv("HOME");

    if (home == NULL || home[0] == '\0') {
        home = "/tmp";
    }
    if (snprintf(g_log_path, sizeof(g_log_path),
                 "%s/Documents/practice_clock_probe.log", home) > 0) {
        g_log_path_ok = 1;
    }
}

static void append_raw(const char *text, size_t len)
{
    FILE *fp;

    if (!g_log_path_ok || text == NULL || len == 0) {
        return;
    }
    fp = fopen(g_log_path, "ab");
    if (fp == NULL) {
        return;
    }
    fwrite(text, 1, len, fp);
    fclose(fp);
}

/* --------------------------------------------------------------- clock */

static uint64_t monotonic_ms(void)
{
    static mach_timebase_info_data_t tb;
    static int tb_ready;
    uint64_t ticks;

    if (!tb_ready) {
        if (mach_timebase_info(&tb) != 0 || tb.denom == 0) {
            tb.numer = 1;
            tb.denom = 1;
        }
        tb_ready = 1;
    }
    ticks = mach_absolute_time();
    return (ticks * (uint64_t)tb.numer) / ((uint64_t)tb.denom * 1000000ull);
}

/* ------------------------------------------------------------- sampler */

static char g_buf[FLUSH_EVERY_LINES * LINEBUF];
static size_t g_buf_len;
static int g_buf_lines;

static void buffer_flush(void)
{
    if (g_buf_len > 0) {
        append_raw(g_buf, g_buf_len);
        g_buf_len = 0;
        g_buf_lines = 0;
    }
}

static void buffer_add(const char *line, size_t len)
{
    if (len == 0 || len > LINEBUF) {
        return;
    }
    if (g_buf_len + len > sizeof(g_buf)) {
        buffer_flush();
    }
    memcpy(g_buf + g_buf_len, line, len);
    g_buf_len += len;
    g_buf_lines += 1;
}

static void *sampler_main(void *arg)
{
    uint64_t prev = monotonic_ms();
    uint64_t seq = 0;
    struct timespec ts;
    (void)arg;

    pthread_setname_np("practice.clock");

    for (;;) {
        char line[LINEBUF];
        uint64_t now, dt;
        int n, gap;

        ts.tv_sec = SAMPLE_INTERVAL_MS / 1000;
        ts.tv_nsec = (long)(SAMPLE_INTERVAL_MS % 1000) * 1000000L;
        nanosleep(&ts, NULL);

        now = monotonic_ms();
        dt = now - prev;
        prev = now;
        seq += 1;
        gap = (dt >= (uint64_t)SAMPLE_INTERVAL_MS * GAP_FACTOR);

        n = snprintf(line, sizeof(line), "s seq=%llu t_ms=%llu dt_ms=%llu%s\n",
                     (unsigned long long)seq, (unsigned long long)now,
                     (unsigned long long)dt, gap ? " gap=1" : "");
        if (n > 0 && (size_t)n < sizeof(line)) {
            buffer_add(line, (size_t)n);
        }
        if (gap || seq == 1) {
            buffer_flush(); /* startup evidence + interesting events are durable */
        } else if (g_buf_lines >= FLUSH_EVERY_LINES) {
            buffer_flush();
        }
    }
    return NULL;
}

/* ----------------------------------------------------------------- ctor */

__attribute__((constructor))
static void practice_clock_probe_ctor(void)
{
    char header[512];
    uint64_t t0 = monotonic_ms();
    time_t now = time(NULL);
    struct tm tmv;
    pthread_t th;
    int n;

    init_log_path();

    if (gmtime_r(&now, &tmv) != NULL) {
        n = snprintf(header, sizeof(header),
                     "# practice_clock_probe v1; framework validation only (no game access)\n"
                     "# build=%s pid=%ld utc=%04d-%02d-%02dT%02d:%02d:%02dZ t0_ms=%llu\n",
                     PRACTICE_CLOCK_PROBE_VERSION, (long)getpid(),
                     tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                     tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                     (unsigned long long)t0);
    } else {
        n = snprintf(header, sizeof(header),
                     "# practice_clock_probe v1; framework validation only (no game access)\n"
                     "# build=%s pid=%ld t0_ms=%llu\n",
                     PRACTICE_CLOCK_PROBE_VERSION, (long)getpid(),
                     (unsigned long long)t0);
    }
    if (n > 0 && (size_t)n < sizeof(header)) {
        append_raw(header, (size_t)n);
    }

    if (pthread_create(&th, NULL, sampler_main, NULL) != 0) {
        static const char fail_msg[] = "# sampler thread start failed\n";
        append_raw(fail_msg, sizeof(fail_msg) - 1);
        return;
    }
    pthread_detach(th);

    syslog(LOG_ERR, "[practice-clock-probe] loaded version=%s",
           PRACTICE_CLOCK_PROBE_VERSION);
}
