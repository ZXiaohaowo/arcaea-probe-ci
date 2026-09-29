/* practice_clock_probe.c - S2a: clock framework + pointer-chain probe + guarded reads (v3).
 *
 * v9 scope (per the 2026-09-29 review), kept in v3:
 *   - NO active calls into game functions. Reads only.
 *   - Guards at every level: pointer plausibility checks before each dereference; both
 *     AudioManager and provider vtable markers verified; the chain is re-acquired on every
 *     sample (no long-term caching); if the chain or the channel container changes during a
 *     snapshot the snapshot is discarded and the event is logged.
 *   - Channel container: begin/end null combinations, ordering, 16-byte stride and count
 *     sanity are all checked before the count is used; on any mismatch the frame is marked
 *     anomalous and no deeper dereference happens.
 *   - Honest limits: non-null does not mean valid; a vtable match does not guarantee object
 *     lifetime; these guards reduce misreads but do NOT replace thread synchronization and do
 *     NOT prove objects cannot be freed. Conclusions are limited to "chain reading held in
 *     the tested scenarios" (see docs/s2a_audio_position_static_findings.md).
 *   - v10 addition: ONE guarded read per task - AM::getBGMPosition() - executed on the main
 *     THREAD (a CFRunLoopSource added to the main runloop; fully linked, no runtime symbol
 *     lookup) with fresh pre-checks (ident ok, chain ready, handle non-null). No other game
 *     function is called. The guard is not a lock; see docs/s2a_v10_prereq_thread_analysis.md.
 *
 * Log: <sandbox>/Documents/practice_clock_probe.log (append-only).
 *   Header:  # practice_clock_probe v3 (chain probe + guarded position reads)
 *            # build=<id> pid=<n> utc=<ISO8601Z> t0_ms=<n>
 *   Ident:   # ident base=0x.. slide=0x.. img=.. magic=<0|1> words=<0|1> vt=<0|1> ...
 *   Reads:   # reads mainhop=<cf-runloop|unavailable>
 *   Sample:  s seq=<n> t_ms=<n> dt_ms=<n> [gap=1]
 *   Chain:   c seq=<n> t_ms=<n> tid=<n> main=<0|1> state=<ready|not-ready|anomaly|changed>
 *            [slot=0x.. gg=0x.. am=0x.. pv=0x.. count=<n> chan0=0x..] [reason=<..>]
 *   Position: p seq=<n> t_ms=<n> t_q=<n> lag_ms=<n> tid=<n> main=<0|1> am=0x.. handle=0x..
 *            ok=<0|1> pos_ms=<n> [reason=<..>]   -- main-thread executed (main=1 expected).
 *   Chain lines are change-driven (heartbeat every 60 samples). Position reads run at <=2 Hz
 *   only while: chain ready, channel-0 handle non-null, handle stable for >=250 ms, and no
 *   previous read is still in flight.
 *
 * Lifecycle policy (unchanged from v1): game pause is not detectable here; backgrounding
 * shows up as a sample gap; exit loses at most the unflushed buffer.
 *
 * Safety rules: no game calls, no heap allocation, no UIKit, no networking; every failure
 * path returns silently into the sampler loop; this module must never abort the host app.
 */
#include <mach-o/dyld.h>
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
#define PRACTICE_CLOCK_PROBE_VERSION "s2a-read-" PRACTICE_BUILD_ID

#define SAMPLE_INTERVAL_MS 1000
#define GAP_FACTOR 3
#define FLUSH_EVERY_LINES 8
#define LINEBUF 320
#define HEARTBEAT_EVERY 60

/* Image-relative offsets (vmaddr - 0x100000000 for this client build; EQUAL to file
 * offsets only for file-backed __TEXT/__DATA ranges - __common/BSS has no file content
 * and is addressed at runtime as base + offset). Verified in
 * docs/s2a_audio_position_static_findings.md. */
#define OFF_SLOT              0x16781d8ULL  /* __common: GameGlobal* slot              */
#define OFF_AM_GETPOS         0xb6b844ULL   /* AudioManager::getBGMPosition()          */
#define OFF_AM_INIT           0xb69940ULL   /* AudioManager::init(AudioProvider*)      */
#define OFF_AM_VT             0x14f90f8ULL  /* AudioManager vtable base                */
#define OFF_AM_VT_SLOT_INIT   0x508ULL      /* vtable slot: init                       */
#define OFF_PV_GETPOS         0x8e4274ULL   /* AudioProviderFMODiOS::getBGMPosition(i) */
#define OFF_PV_VT             0x14bb930ULL  /* provider vtable base                    */
#define OFF_PV_VT_SLOT_GETPOS 0x38ULL       /* vtable slot: getBGMPosition             */
#define OFF_GG_AM             0x10ULL       /* GameGlobal -> AudioManager              */
#define OFF_AM_PROVIDER       0x280ULL      /* AudioManager -> provider                */
#define OFF_PV_VEC_BEGIN      0x38ULL       /* provider BGM channel vector begin       */
#define OFF_PV_VEC_END        0x40ULL       /* provider BGM channel vector end         */
#define CHAN_ELEM_SIZE        16ULL         /* vector element stride                   */
#define CHAN_COUNT_CAP        64ULL         /* sanity cap (game reserves 10)           */

/* First 8 bytes of each key function (little-endian uint64 of the instruction pair). */
#define MARK64_GETPOS_AP 0xa9017bfdd10083ffULL  /* sub sp,sp,#0x20; stp x29,x30,[sp,#0x10] */
#define MARK64_GETPOS_AM 0xf9400008f9414000ULL  /* ldr x0,[x0,#0x280]; ldr x8,[x0]         */
#define MARK64_AM_INIT   0xa9017bfda9be4ff4ULL  /* stp x20,x19,[sp,#-0x20]!; stp x29,x30   */

__attribute__((visibility("default")))
const char practice_clock_probe_version[] = PRACTICE_CLOCK_PROBE_VERSION;

/* Main-thread hop for the guarded reads (v10c): a CFRunLoopSource added to the main
 * runloop. Fully LINKED (CoreFoundation) - no runtime symbol lookup, which failed for
 * dispatch on this OS in two different forms (libSystem handle + RTLD_DEFAULT). The
 * perform callback runs on the main thread; p-lines carry main= to prove it. */
typedef struct __CFRunLoop *cf_runloop_ref;
typedef struct __CFRunLoopSource *cf_runloop_source_ref;
typedef struct {
    long version;
    void *info;
    const void *retain;
    const void *release;
    const void *copy_description;
    const void *equal;
    const void *hash;
    const void *schedule;
    const void *cancel;
    void (*perform)(void *info);
} cf_runloop_source_context_t;
extern cf_runloop_ref CFRunLoopGetMain(void);
extern cf_runloop_source_ref CFRunLoopSourceCreate(void *allocator, long order,
                                                   cf_runloop_source_context_t *context);
extern void CFRunLoopAddSource(cf_runloop_ref rl, cf_runloop_source_ref source,
                               const void *mode);
extern void CFRunLoopSourceSignal(cf_runloop_source_ref source);
extern void CFRunLoopWakeUp(cf_runloop_ref rl);
extern const void *kCFRunLoopCommonModes;

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

/* -------------------------------------------------------- image identity */

static uint64_t g_base;      /* main image runtime base */
static uint64_t g_slide;
static char g_imgname[160];
static int g_magic_ok, g_words_ok, g_vt_ok, g_ident_ok;

static void ident_check(void)
{
    const void *h = (const void *)_dyld_get_image_header(0);
    const char *name = _dyld_get_image_name(0);

    g_base = (uint64_t)(uintptr_t)h;
    g_slide = (uint64_t)(uintptr_t)_dyld_get_image_vmaddr_slide(0);
    if (name != NULL) {
        snprintf(g_imgname, sizeof(g_imgname), "%s", name);
    } else {
        snprintf(g_imgname, sizeof(g_imgname), "?");
    }
    if (g_base == 0) {
        return;
    }
    g_magic_ok = (*(volatile uint32_t *)(uintptr_t)(g_base + 0) == 0xfeedfacfu);
    g_words_ok =
        (*(volatile uint64_t *)(uintptr_t)(g_base + OFF_PV_GETPOS) == MARK64_GETPOS_AP) &&
        (*(volatile uint64_t *)(uintptr_t)(g_base + OFF_AM_GETPOS) == MARK64_GETPOS_AM) &&
        (*(volatile uint64_t *)(uintptr_t)(g_base + OFF_AM_INIT) == MARK64_AM_INIT);
    g_vt_ok =
        (*(volatile uint64_t *)(uintptr_t)(g_base + OFF_PV_VT + OFF_PV_VT_SLOT_GETPOS) ==
         g_base + OFF_PV_GETPOS) &&
        (*(volatile uint64_t *)(uintptr_t)(g_base + OFF_AM_VT + OFF_AM_VT_SLOT_INIT) ==
         g_base + OFF_AM_INIT);
    g_ident_ok = g_magic_ok && g_words_ok && g_vt_ok;
}

/* ------------------------------------------------------- pointer checks */

static int ptr_plausible(uint64_t p)
{
    return p >= 0x100000000ull && p < (1ull << 47) && (p & 7ull) == 0;
}

/* -------------------------------------------------------- chain snapshot */

enum { CH_NOT_READY = 0, CH_READY = 1, CH_ANOMALY = 2, CH_CHANGED = 3 };

typedef struct {
    uint64_t slot, gg, am, pv, begin, end, count, chan0;
    int state;
    const char *reason;
} chain_snap_t;

static void snapshot_chain(chain_snap_t *cs)
{
    uint64_t slot1, am, pv, begin, end, bytes, count, slot2, begin2, end2;
    int settled = 0;

    memset(cs, 0, sizeof(*cs));
    cs->state = CH_NOT_READY;
    cs->reason = "";
    if (g_base == 0) {
        cs->state = CH_ANOMALY;
        cs->reason = "no-base";
        return;
    }

    slot1 = *(volatile uint64_t *)(uintptr_t)(g_base + OFF_SLOT);
    cs->slot = slot1;
    cs->gg = slot1;
    if (slot1 == 0) {
        cs->reason = "slot-null";          /* boot not finished / not set yet */
        return;
    }
    if (!ptr_plausible(slot1)) {
        cs->state = CH_ANOMALY;
        cs->reason = "slot-bad";
        return;
    }

    am = *(volatile uint64_t *)(uintptr_t)(slot1 + OFF_GG_AM);
    cs->am = am;
    if (am == 0) {
        cs->reason = "am-null";
        return;
    }
    if (!ptr_plausible(am)) {
        cs->state = CH_ANOMALY;
        cs->reason = "am-bad";
        return;
    }
    if (*(volatile uint64_t *)(uintptr_t)am != g_base + OFF_AM_VT) {
        cs->state = CH_ANOMALY;
        cs->reason = "am-vt";
        return;
    }

    pv = *(volatile uint64_t *)(uintptr_t)(am + OFF_AM_PROVIDER);
    cs->pv = pv;
    if (pv == 0) {
        cs->reason = "pv-null";
        return;
    }
    if (!ptr_plausible(pv)) {
        cs->state = CH_ANOMALY;
        cs->reason = "pv-bad";
        return;
    }
    if (*(volatile uint64_t *)(uintptr_t)pv != g_base + OFF_PV_VT) {
        cs->state = CH_ANOMALY;
        cs->reason = "pv-vt";
        return;
    }

    begin = *(volatile uint64_t *)(uintptr_t)(pv + OFF_PV_VEC_BEGIN);
    end = *(volatile uint64_t *)(uintptr_t)(pv + OFF_PV_VEC_END);
    cs->begin = begin;
    cs->end = end;

    if (begin == 0 && end == 0) {
        cs->count = 0;
        settled = 1;
    } else if (begin == 0 || end == 0) {
        cs->state = CH_ANOMALY;
        cs->reason = "vec-nullpair";
        return;
    } else if (!ptr_plausible(begin) || !ptr_plausible(end)) {
        cs->state = CH_ANOMALY;
        cs->reason = "vec-bad";
        return;
    } else if (end < begin) {
        cs->state = CH_ANOMALY;
        cs->reason = "vec-reversed";
        return;
    } else {
        bytes = end - begin;
        if (bytes % CHAN_ELEM_SIZE != 0) {
            cs->state = CH_ANOMALY;
            cs->reason = "vec-stride";
            return;
        }
        count = bytes / CHAN_ELEM_SIZE;
        if (count > CHAN_COUNT_CAP) {
            cs->state = CH_ANOMALY;
            cs->reason = "vec-count";
            return;
        }
        cs->count = count;
        if (count >= 1) {
            cs->chan0 = *(volatile uint64_t *)(uintptr_t)(begin + 8);
        }
        settled = 1;
    }

    if (settled) {
        /* mid-read change detection: re-read the root slot and the vector ends */
        slot2 = *(volatile uint64_t *)(uintptr_t)(g_base + OFF_SLOT);
        begin2 = *(volatile uint64_t *)(uintptr_t)(pv + OFF_PV_VEC_BEGIN);
        end2 = *(volatile uint64_t *)(uintptr_t)(pv + OFF_PV_VEC_END);
        if (slot2 != slot1 || begin2 != begin || end2 != end) {
            cs->state = CH_CHANGED;
            cs->reason = "changed-midread";
            return;
        }
        cs->state = CH_READY;
    }
}

static int chain_differs(const chain_snap_t *a, const chain_snap_t *b)
{
    return a->state != b->state || a->slot != b->slot || a->am != b->am ||
           a->pv != b->pv || a->count != b->count || a->chan0 != b->chan0 ||
           strcmp(a->reason, b->reason) != 0;
}

static const char *state_name(int s)
{
    switch (s) {
    case CH_READY: return "ready";
    case CH_ANOMALY: return "anomaly";
    case CH_CHANGED: return "changed";
    default: return "not-ready";
    }
}

/* ------------------------------------- guarded position reads (v10, main thread) */

static cf_runloop_ref g_main_runloop;
static cf_runloop_source_ref g_read_source;

typedef struct {
    uint64_t q_seq;
    uint64_t q_t_ms;
} read_task_t;

static read_task_t g_read_task;
static volatile int g_read_inflight;
static uint64_t g_last_handle;
static uint64_t g_last_handle_change_ms;
static uint64_t g_last_enqueue_ms;

/* main-thread-only buffer for p-lines (never touched by the sampler thread) */
static char g_pbuf[4 * LINEBUF];
static size_t g_pbuf_len;
static int g_pbuf_lines;

static void pflush(void)
{
    if (g_pbuf_len > 0) {
        append_raw(g_pbuf, g_pbuf_len);
        g_pbuf_len = 0;
        g_pbuf_lines = 0;
    }
}

static void padd(const char *s, size_t n)
{
    if (n == 0 || n > LINEBUF) {
        return;
    }
    if (g_pbuf_len + n > sizeof(g_pbuf)) {
        pflush();
    }
    memcpy(g_pbuf + g_pbuf_len, s, n);
    g_pbuf_len += n;
    g_pbuf_lines += 1;
    if (g_pbuf_lines >= 4) {
        pflush();
    }
}

static void read_trampoline(void *ctx)   /* runs on the main thread (runloop source) */
{
    read_task_t *task = (read_task_t *)ctx;
    chain_snap_t cs;
    uint64_t now;
    uint64_t tid = 0;
    int pos = -1;
    int ok = 0;
    const char *reason = "";
    char line[LINEBUF];
    int n;

    snapshot_chain(&cs);
    if (!g_ident_ok) {
        reason = "ident";
    } else if (cs.state != CH_READY) {
        reason = "chain";
    } else if (cs.chan0 == 0) {
        reason = "handle-null";
    } else {
        typedef int (*getpos_fn)(void *);
        getpos_fn fn = (getpos_fn)(uintptr_t)(g_base + OFF_AM_GETPOS);
        pos = fn((void *)(uintptr_t)cs.am);
        ok = 1;
    }
    now = monotonic_ms();
    pthread_threadid_np(NULL, &tid);
    n = snprintf(line, sizeof(line),
                 "p seq=%llu t_ms=%llu t_q=%llu lag_ms=%llu tid=%llu main=%d am=%llx handle=%llx ok=%d pos_ms=%d",
                 (unsigned long long)task->q_seq, (unsigned long long)now,
                 (unsigned long long)task->q_t_ms, (unsigned long long)(now - task->q_t_ms),
                 (unsigned long long)tid, pthread_main_np() ? 1 : 0,
                 (unsigned long long)cs.am, (unsigned long long)cs.chan0, ok, pos);
    if (!ok && n > 0 && (size_t)n < sizeof(line) - 32) {
        n += snprintf(line + n, sizeof(line) - (size_t)n, " reason=%s", reason);
    }
    if (n > 0 && (size_t)n < sizeof(line) - 2) {
        line[n++] = '\n';
        line[n] = '\0';
        padd(line, (size_t)n);
    }
    if (!ok) {
        pflush();   /* keep skip events durable */
    }
    g_read_inflight = 0;
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

static void log_chain_line(uint64_t seq, uint64_t t_ms, int heartbeat_mode,
                           const chain_snap_t *cs)
{
    char line[LINEBUF];
    uint64_t tid = 0;
    int n;

    pthread_threadid_np(NULL, &tid);
    n = snprintf(line, sizeof(line),
                 "c seq=%llu t_ms=%llu tid=%llu main=%d hb=%d state=%s",
                 (unsigned long long)seq, (unsigned long long)t_ms, (unsigned long long)tid,
                 pthread_main_np() ? 1 : 0, heartbeat_mode, state_name(cs->state));
    if (n > 0 && (size_t)n < sizeof(line) && cs->slot != 0) {
        n += snprintf(line + n, sizeof(line) - (size_t)n,
                      " slot=%llx gg=%llx am=%llx pv=%llx",
                      (unsigned long long)cs->slot, (unsigned long long)cs->gg,
                      (unsigned long long)cs->am, (unsigned long long)cs->pv);
    }
    if (n > 0 && (size_t)n < sizeof(line) && cs->state == CH_READY && cs->pv != 0) {
        n += snprintf(line + n, sizeof(line) - (size_t)n,
                      " count=%llu", (unsigned long long)cs->count);
        if (cs->count >= 1) {
            n += snprintf(line + n, sizeof(line) - (size_t)n,
                          " chan0=%llx", (unsigned long long)cs->chan0);
        }
    }
    if (n > 0 && (size_t)n < sizeof(line) && cs->reason[0] != '\0') {
        n += snprintf(line + n, sizeof(line) - (size_t)n, " reason=%s", cs->reason);
    }
    if (n > 0 && (size_t)n < sizeof(line) - 1) {
        line[n++] = '\n';
        line[n] = '\0';
        buffer_add(line, (size_t)n);
    }
}

static void *sampler_main(void *arg)
{
    uint64_t prev = monotonic_ms();
    uint64_t seq = 0;
    struct timespec ts;
    chain_snap_t last;
    int have_last = 0;
    int hb_countdown = 0;
    (void)arg;

    pthread_setname_np("practice.clock");

    /* initial state line: log the first snapshot as soon as possible */
    memset(&last, 0, sizeof(last));
    last.state = -1;

    for (;;) {
        char line[LINEBUF];
        uint64_t now, dt;
        chain_snap_t cs;
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
            buffer_flush();
        }

        /* read-only chain probe: fresh snapshot every sample, no caching */
        snapshot_chain(&cs);
        {
            int differ = !have_last || chain_differs(&cs, &last);
            if (differ || hb_countdown <= 0) {
                log_chain_line(seq, now, differ ? 0 : 1, &cs);
                last = cs;
                have_last = 1;
                hb_countdown = HEARTBEAT_EVERY;
            } else {
                hb_countdown -= 1;
            }
        }

        /* guarded position read enqueue (v10): main queue, stability-gated */
        if (cs.state == CH_READY) {
            if (cs.chan0 != g_last_handle) {
                g_last_handle = cs.chan0;
                g_last_handle_change_ms = now;
            }
            if (g_ident_ok && g_read_source != NULL && cs.chan0 != 0 &&
                (now - g_last_handle_change_ms) >= 250 &&
                (now - g_last_enqueue_ms) >= 500 &&
                !g_read_inflight) {
                if (__sync_bool_compare_and_swap(&g_read_inflight, 0, 1)) {
                    g_read_task.q_seq = seq;
                    g_read_task.q_t_ms = now;
                    g_last_enqueue_ms = now;
                    CFRunLoopSourceSignal(g_read_source);
                    CFRunLoopWakeUp(g_main_runloop);
                }
            }
        }

        if (cs.state == CH_ANOMALY || cs.state == CH_CHANGED) {
            buffer_flush();   /* interesting events are durable */
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
    ident_check();

    if (gmtime_r(&now, &tmv) != NULL) {
        n = snprintf(header, sizeof(header),
                     "# practice_clock_probe v3 (chain probe + guarded position reads)\n"
                     "# build=%s pid=%ld utc=%04d-%02d-%02dT%02d:%02d:%02dZ t0_ms=%llu\n",
                     PRACTICE_CLOCK_PROBE_VERSION, (long)getpid(),
                     tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                     tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                     (unsigned long long)t0);
    } else {
        n = snprintf(header, sizeof(header),
                     "# practice_clock_probe v3 (chain probe + guarded position reads)\n"
                     "# build=%s pid=%ld t0_ms=%llu\n",
                     PRACTICE_CLOCK_PROBE_VERSION, (long)getpid(),
                     (unsigned long long)t0);
    }
    if (n > 0 && (size_t)n < sizeof(header)) {
        append_raw(header, (size_t)n);
    }

    n = snprintf(header, sizeof(header),
                 "# ident base=0x%llx slide=0x%llx img=%s magic=%d words=%d vt=%d\n",
                 (unsigned long long)g_base, (unsigned long long)g_slide, g_imgname,
                 g_magic_ok, g_words_ok, g_vt_ok);
    if (n > 0 && (size_t)n < sizeof(header)) {
        append_raw(header, (size_t)n);
    }

    {
        cf_runloop_source_context_t ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.info = &g_read_task;
        ctx.perform = read_trampoline;

        g_main_runloop = CFRunLoopGetMain();
        if (g_main_runloop != NULL) {
            g_read_source = CFRunLoopSourceCreate(NULL, 0, &ctx);
            if (g_read_source != NULL) {
                CFRunLoopAddSource(g_main_runloop, g_read_source, kCFRunLoopCommonModes);
            }
        }
        n = snprintf(header, sizeof(header), "# reads mainhop=%s\n",
                     (g_read_source != NULL) ? "cf-runloop" : "unavailable");
        if (n > 0 && (size_t)n < sizeof(header)) {
            append_raw(header, (size_t)n);
        }
    }

    if (pthread_create(&th, NULL, sampler_main, NULL) != 0) {
        static const char fail_msg[] = "# sampler thread start failed\n";
        append_raw(fail_msg, sizeof(fail_msg) - 1);
        return;
    }
    pthread_detach(th);

    syslog(LOG_ERR, "[practice-clock-probe] loaded version=%s ident=%d",
           PRACTICE_CLOCK_PROBE_VERSION, g_magic_ok && g_words_ok && g_vt_ok);
}
