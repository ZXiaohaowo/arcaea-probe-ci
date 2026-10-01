/* practice_clock_probe.c - S3a: read-only verification - rate-entry probe (v7).
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
 *   - v11 addition (S2b): read-only chart-time probes in the same callback - the BSS mirror
 *     and a GameTimeline chain candidate, marker-validated. Device run 2026-10-01: the chain
 *     candidate proved wrong (read text/data bytes at its middle hop) and its loose guard let
 *     a garbage value through to a dereference that crashed the main thread; the mirror
 *     offset was also wrong by 0x1000000 (0x6539c0; correct: 0x16539c0).
 *   - v12 fix (S2b): mirror offset corrected; the disproven chain is replaced by a guarded
 *     DISCOVERY CRAWL for the scene/timeline (scene factory marker 0x14c0d08, timeline
 *     factory marker 0x14d0548). All probing reads go through mach_vm_read_overwrite
 *     (weakly imported): reads of unmapped or garbage addresses return a failure instead
 *     of faulting. Identity, liveness and concurrency of objects remain separately
 *     unverified - a type marker proves WHAT an object is, never that it is the CURRENT
 *     active one.
 *   - v13 fix (S2b, after the 2026-10-01 review): ALL new S2b reads (mirror, dump,
 *     singleton slot, crawl, walk) go through the single checked safe-read interface;
 *     the whole S2b path is skipped when the identity check fails; read failures are
 *     logged explicitly (never zeros as valid data). Weak symbols are FUNCTION symbols
 *     only (task_self_trap + mach_vm_read_overwrite), address-checked before any call.
 *     The discovered path is re-walked from the root object every cycle (no stale-object
 *     reads). The crawl has a per-cycle read budget, a growing backoff interval, work
 *     counters and an off switch (build flag PCP_CRAWL / env PCP_NO_CRAWL). See
 *     docs/s2b_review_2026-10-01.md.
 *   - v14 (S3a read-only verification): module v7 adds ONE read call - ChannelControl::
 *     getPitch (wrapper base+0x10e6540) on the current BGM channel handle - to verify the
 *     future rate-entry chain is callable in this context and to capture baseline pitch
 *     values. Same guard class as getBGMPosition; the FMOD result code is logged. The
 *     discovery crawl now defaults OFF (PCP_CRAWL=0; switch and budget kept). No writes
 *     of any kind are performed. See docs/s3a_control_explainer_draft.md.
 *
 * Log: <sandbox>/Documents/practice_clock_probe.log (append-only).
 *   Header:  # practice_clock_probe v7 (S3a read-only verification: rate-entry probe)
 *            # build=<id> pid=<n> utc=<ISO8601Z> t0_ms=<n>
 *   Ident:   # ident base=0x.. slide=0x.. img=.. magic=<0|1> words=<0|1> vt=<0|1> ...
 *   Reads:   # reads mainhop=<cf-runloop|unavailable>
 *   Saferead:# saferead=<mach-vm/1|off/0> crawl=<on|off>
 *   Sample:  s seq=<n> t_ms=<n> dt_ms=<n> [gap=1]
 *   Chain:   c seq=<n> t_ms=<n> tid=<n> main=<0|1> state=<ready|not-ready|anomaly|changed>
 *            [slot=0x.. gg=0x.. am=0x.. pv=0x.. count=<n> chan0=0x..] [reason=<..>]
 *   Position: p seq=<n> t_ms=<n> t_q=<n> lag_ms=<n> tid=<n> main=<0|1> am=0x.. handle=0x..
 *            ok=<0|1> pos_ms=<n> pitch=<n|na> pchret=<n> safe=<0|1> reads=<n>/<n>/<n>/<n>
 *            cw=<0|1> cD=<0|1> ctA=<0|1> ctM=<n|na> ctMok=<0|1> xf=<token>
 *            [path=<hub><style>+0x<off> tl=0x.. t20=<n> t24=<n> t28=<n> f2c=<n> f2d=<n> f2e=<n>]
 *            w=<16 x %08x|na> [reason=<..>]
 *            -- main-thread executed (main=1 expected); pitch = BGM channel pitch in
 *            milli-units (v14 read-only probe; na when the call failed) and pchret its
 *            FMOD result code; safe = mach safe reads available; reads = safe-read
 *            attempts/filtered/failed/budget-hit this cycle; cw = crawl
 *            switch state; cD = a path is currently cached (re-walked from the root every
 *            cycle); ctA = timeline fields read this cycle; ctM = BSS mirror - <n> only
 *            when its checked read succeeded, else na; xf = last failure token
 *            (none|mirror|dump|walk|scan|budget|fields|ident|nomach); w = 16 u32 words at
 *            0x16539a0 (mirror at index 8), or na.
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
#define PRACTICE_CLOCK_PROBE_VERSION "s3a-verify-" PRACTICE_BUILD_ID

#define SAMPLE_INTERVAL_MS 1000
#define GAP_FACTOR 3
#define FLUSH_EVERY_LINES 8
#define LINEBUF 576
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
#define OFF_GETPITCH          0x10e6540ULL  /* ChannelControl::getPitch(handle,float*)  */
#define OFF_PV_VT             0x14bb930ULL  /* provider vtable base                    */
#define OFF_PV_VT_SLOT_GETPOS 0x38ULL       /* vtable slot: getBGMPosition             */
#define OFF_GG_AM             0x10ULL       /* GameGlobal -> AudioManager              */
#define OFF_AM_PROVIDER       0x280ULL      /* AudioManager -> provider                */
#define OFF_PV_VEC_BEGIN      0x38ULL       /* provider BGM channel vector begin       */
#define OFF_PV_VEC_END        0x40ULL       /* provider BGM channel vector end         */
#define CHAN_ELEM_SIZE        16ULL         /* vector element stride                   */
#define CHAN_COUNT_CAP        64ULL         /* sanity cap (game reserves 10)           */

/* S2b chart-time probes (v12; see docs/s2b_static_findings.md). */
#define OFF_CT_MIRROR         0x16539c0ULL  /* BSS mirror: last computed chart time    */
#define OFF_CT_DUMP_BASE      0x16539a0ULL  /* 16 u32 words around the mirror          */
#define OFF_SINGLETON         0x1660280ULL  /* app singleton global                    */
#define OFF_GG_D88            0x88ULL       /* GameGlobal -> manager (scene registered)*/
#define OFF_GG_O78            0x78ULL       /* GameGlobal -> object (early candidate)  */
#define OFF_HUB_SCENE_A       0x3a0ULL      /* controller -> scene                     */
#define OFF_HUB_SCENE_B       0x2c0ULL      /* alt holder -> scene                     */
#define OFF_SCENE_TL          0x30ULL       /* scene -> GameTimeline                   */
#define SCENE_MARKER_OFF      0x14c0d08ULL  /* *(void**)scene == base + this (factory) */
#define TL_MARKER_OFF         0x14d0548ULL  /* *(void**)tl == base + this (factory)    */
#define OFF_TL_TIME           0x20ULL       /* timeline: (t20,t24) word                */
#define OFF_TL_W28            0x28ULL       /* timeline: (t28,f2c,f2d,f2e) word        */
#define CT_DUMP_WORDS         16

/* First 8 bytes of each key function (little-endian uint64 of the instruction pair). */
#define MARK64_GETPOS_AP 0xa9017bfdd10083ffULL  /* sub sp,sp,#0x20; stp x29,x30,[sp,#0x10] */
#define MARK64_GETPOS_AM 0xf9400008f9414000ULL  /* ldr x0,[x0,#0x280]; ldr x8,[x0]         */
#define MARK64_AM_INIT   0xa9017bfda9be4ff4ULL  /* stp x20,x19,[sp,#-0x20]!; stp x29,x30   */
#define MARK64_GETPITCH  0xa91257f6d10543ffULL  /* sub sp,sp,#0x150; stp x22,x21,[sp,#0x120] */

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
        (*(volatile uint64_t *)(uintptr_t)(g_base + OFF_GETPITCH) == MARK64_GETPITCH) &&
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

/* ------------------------------------------- safe reads + timeline discovery */

/* ------------------------------------------- safe reads + timeline discovery */

/* Weak FUNCTION imports only, address-checked before any call (no weak data variable is
 * read unguarded; the mach_task_self_ fallback below checks the variable address first).
 * mach_vm_read_overwrite turns reads of unmapped/garbage addresses into clean failures;
 * if the symbols are absent at runtime the whole S2b read path is skipped and nothing is
 * dereferenced. Identity, liveness and concurrency of objects remain separately
 * unverified. */
extern unsigned int task_self_trap(void) __attribute__((weak_import));
extern unsigned int mach_task_self_ __attribute__((weak_import));
extern int mach_vm_read_overwrite(unsigned int task, unsigned long long addr,
                                  unsigned long long size, unsigned long long data,
                                  unsigned long long *out_size) __attribute__((weak_import));

static int g_safe_ok;
static unsigned int g_task_port;

static void saferead_init(void)
{
    g_safe_ok = 0;
    g_task_port = 0;
    if (mach_vm_read_overwrite == NULL) {
        return;
    }
    if (task_self_trap != NULL) {
        g_task_port = task_self_trap();
    }
    if (g_task_port == 0 && &mach_task_self_ != NULL) {
        g_task_port = mach_task_self_;   /* weak-var address checked before reading */
    }
    if (g_task_port != 0) {
        g_safe_ok = 1;
    }
}

/* Per-cycle work accounting for the S2b read path. */
#define SR_BUDGET_PER_CYCLE 1500
static int g_sr_budget;
static int g_sr_reads;
static int g_sr_filtered;
static int g_sr_failed;
static int g_sr_budget_hit;

/* Heuristic PRE-FILTER for values treated as object pointers. Out-of-range values are
 * counted as "filtered" - never as proof that an object does not exist. */
static int ptr_range_ok(uint64_t p)
{
    return (p & 7ull) == 0 && (p - 0x100000000ull) < 0x100000000ull;   /* [4 GiB, 8 GiB) */
}

/* Single checked read interface: the FULL length must be read or the read fails. */
static int safe_read(uint64_t addr, void *dst, uint64_t len)
{
    unsigned long long got = 0;
    if (!g_safe_ok) {
        return 0;
    }
    if (g_sr_budget <= 0) {
        g_sr_budget_hit = 1;
        return 0;
    }
    g_sr_budget -= 1;
    g_sr_reads += 1;
    if (mach_vm_read_overwrite(g_task_port, (unsigned long long)addr, len,
                               (unsigned long long)(uintptr_t)dst, &got) != 0 || got != len) {
        g_sr_failed += 1;
        return 0;
    }
    return 1;
}

static int try_read64(uint64_t addr, uint64_t *out)
{
    if (!ptr_range_ok(addr)) {
        g_sr_filtered += 1;
        return 0;
    }
    return safe_read(addr, out, 8);
}

/* Discovered path description (re-walked from the ROOT object every cycle; a type marker
 * proves what an object is, not that it is the current active one). */
static int g_hit_valid;
static char g_hit_hub;           /* 'g' gg, 's' singleton, 'd' *(gg+0x88), 'o' *(gg+0x78) */
static int g_hit_style;          /* 1 v=scene; 2 *(v+0x3a0)=scene; 3 *(v+0x2c0)=scene;
                                    4 *(v+0x30)=tl (v = a holder object)                */
static uint64_t g_hit_off;       /* hub slot offset; 0 = the hub object itself           */

/* Crawl scheduling: budgeted attempts with growing backoff after failures, and an
 * independent off switch (build: -DPCP_CRAWL=0, runtime: env PCP_NO_CRAWL). */
#ifndef PCP_CRAWL
#define PCP_CRAWL 0        /* review 2026-10-01: discovery scans default OFF */
#endif
#define SCAN_BASE_INTERVAL 30
static int g_crawl_enabled;
static uint32_t g_scan_fail;
static uint64_t g_scan_next_seq;

static void crawl_init(void)
{
    g_crawl_enabled = PCP_CRAWL ? 1 : 0;
    if (g_crawl_enabled && getenv("PCP_NO_CRAWL") != NULL) {
        g_crawl_enabled = 0;
    }
}

static int is_scene(uint64_t scene)
{
    uint64_t v = 0;
    return scene != 0 && try_read64(scene, &v) && v == g_base + SCENE_MARKER_OFF;
}

static int is_tl(uint64_t cand, uint64_t *tl)
{
    uint64_t v = 0;
    if (cand == 0 || !try_read64(cand, &v) || v != g_base + TL_MARKER_OFF) {
        return 0;
    }
    *tl = cand;
    return 1;
}

static int tl_read_fields(uint64_t tl, int *t20, int *t24, int *t28,
                          int *f2c, int *f2d, int *f2e)
{
    uint64_t w20 = 0, w28 = 0;
    if (!try_read64(tl + OFF_TL_TIME, &w20) || !try_read64(tl + OFF_TL_W28, &w28)) {
        return 0;
    }
    *t20 = (int)(uint32_t)w20;
    *t24 = (int)(uint32_t)(w20 >> 32);
    *t28 = (int)(uint32_t)w28;
    *f2c = (int)((w28 >> 32) & 0xffull);
    *f2d = (int)((w28 >> 40) & 0xffull);
    *f2e = (int)((w28 >> 48) & 0xffull);
    return 1;
}

static int crawl_slot(char hub, uint64_t off, uint64_t v, uint64_t *tl)
{
    uint64_t scene = 0, t = 0;
    if (!ptr_range_ok(v)) {
        g_sr_filtered += 1;      /* filtered, not "absent" */
        return 0;
    }
    if (is_scene(v) && try_read64(v + OFF_SCENE_TL, &t) && is_tl(t, tl)) {
        g_hit_hub = hub; g_hit_style = 1; g_hit_off = off;
        return 1;
    }
    if (try_read64(v + OFF_HUB_SCENE_A, &scene) && is_scene(scene) &&
        try_read64(scene + OFF_SCENE_TL, &t) && is_tl(t, tl)) {
        g_hit_hub = hub; g_hit_style = 2; g_hit_off = off;
        return 1;
    }
    if (try_read64(v + OFF_HUB_SCENE_B, &scene) && is_scene(scene) &&
        try_read64(scene + OFF_SCENE_TL, &t) && is_tl(t, tl)) {
        g_hit_hub = hub; g_hit_style = 3; g_hit_off = off;
        return 1;
    }
    if (try_read64(v + OFF_SCENE_TL, &t) && is_tl(t, tl)) {
        g_hit_hub = hub; g_hit_style = 4; g_hit_off = off;
        return 1;
    }
    return 0;
}

static int crawl_hub(char hub, uint64_t base, uint64_t max_off, uint64_t *tl)
{
    uint64_t v = 0, off;
    if (!ptr_range_ok(base)) {
        g_sr_filtered += 1;
        return 0;
    }
    if (crawl_slot(hub, 0, base, tl)) {          /* the hub object itself */
        return 1;
    }
    for (off = 8; off <= max_off; off += 8) {
        if (try_read64(base + off, &v) && crawl_slot(hub, off, v, tl)) {
            return 1;
        }
    }
    return 0;
}

static int find_timeline(uint64_t gg, uint64_t *tl)
{
    uint64_t hub = 0, single = 0;
    if (crawl_hub('g', gg, 0xf0, tl)) {
        return 1;
    }
    if (safe_read(g_base + OFF_SINGLETON, &single, 8) && crawl_hub('s', single, 0xf0, tl)) {
        return 1;
    }
    if (try_read64(gg + OFF_GG_D88, &hub) && crawl_hub('d', hub, 0x58, tl)) {
        return 1;
    }
    if (try_read64(gg + OFF_GG_O78, &hub) && crawl_hub('o', hub, 0x58, tl)) {
        return 1;
    }
    return 0;
}

/* Re-resolve the hub from the CURRENT root objects every cycle (d88/o78 are GameGlobal
 * fields and must be re-read; the singleton is a static slot re-read via safe_read). */
static int resolve_hub(char hub, uint64_t gg, uint64_t *base)
{
    uint64_t v = 0;
    if (hub == 'g') {
        *base = gg;
        return 1;
    }
    if (hub == 's') {
        if (!safe_read(g_base + OFF_SINGLETON, &v, 8)) {
            return 0;
        }
    } else if (hub == 'd') {
        if (!try_read64(gg + OFF_GG_D88, &v)) {
            return 0;
        }
    } else if (hub == 'o') {
        if (!try_read64(gg + OFF_GG_O78, &v)) {
            return 0;
        }
    } else {
        return 0;
    }
    *base = v;
    return 1;
}

/* Full re-walk from the root: hub -> slot -> scene (marker) -> timeline (marker). */
static int walk_hit(uint64_t gg, uint64_t *tl)
{
    uint64_t hub = 0, v = 0, scene = 0, t = 0;
    if (g_hit_style < 1 || g_hit_style > 4) {
        return 0;
    }
    if (!resolve_hub(g_hit_hub, gg, &hub) || !ptr_range_ok(hub)) {
        g_sr_filtered += 1;
        return 0;
    }
    if (g_hit_off == 0) {
        v = hub;
    } else if (!try_read64(hub + g_hit_off, &v)) {
        return 0;
    }
    if (!ptr_range_ok(v)) {
        g_sr_filtered += 1;
        return 0;
    }
    switch (g_hit_style) {
    case 1:
        scene = v;
        break;
    case 2:
        if (!try_read64(v + OFF_HUB_SCENE_A, &scene)) {
            return 0;
        }
        break;
    case 3:
        if (!try_read64(v + OFF_HUB_SCENE_B, &scene)) {
            return 0;
        }
        break;
    default:    /* style 4: v is a holder whose +0x30 is the timeline */
        return try_read64(v + OFF_SCENE_TL, &t) && is_tl(t, tl);
    }
    if (!is_scene(scene)) {
        return 0;
    }
    return try_read64(scene + OFF_SCENE_TL, &t) && is_tl(t, tl);
}

/* Runs on the main thread; every read goes through the checked safe-read interface.
 * The crawl runs only when scheduled (backoff), never on every time sample. */
static void ct_probe(uint64_t gg, uint64_t *tl, int *ctA, int *t20, int *t24, int *t28,
                     int *f2c, int *f2d, int *f2e, char *path, size_t pathlen,
                     const char **xf, uint64_t seq)
{
    *ctA = 0;
    path[0] = '\0';
    if (!g_safe_ok || !ptr_range_ok(gg)) {
        return;
    }
    if (g_hit_valid && !walk_hit(gg, tl)) {
        g_hit_valid = 0;                    /* path broke (scene recreated): re-locate */
        *xf = "walk";
    }
    if (!g_hit_valid && g_crawl_enabled && seq >= g_scan_next_seq) {
        if (find_timeline(gg, tl)) {
            g_hit_valid = 1;
            g_scan_fail = 0;
            g_scan_next_seq = seq + SCAN_BASE_INTERVAL;
        } else {
            if (g_scan_fail < 6) {
                g_scan_fail += 1;
            }
            g_scan_next_seq = seq + (uint64_t)(SCAN_BASE_INTERVAL << g_scan_fail);
            *xf = g_sr_budget_hit ? "budget" : "scan";
        }
    }
    if (!g_hit_valid) {
        return;
    }
    if (!tl_read_fields(*tl, t20, t24, t28, f2c, f2d, f2e)) {
        *xf = "fields";
        return;
    }
    *ctA = 1;
    *xf = "none";
    snprintf(path, pathlen, "%c%d+0x%x", g_hit_hub, g_hit_style, (unsigned)g_hit_off);
}

static void read_trampoline(void *ctx)   /* runs on the main thread (runloop source) */
{
    read_task_t *task = (read_task_t *)ctx;
    chain_snap_t cs;
    uint64_t now;
    uint64_t tid = 0;
    uint64_t tl = 0;
    uint32_t words[CT_DUMP_WORDS] = {0};
    int pos = -1;
    int pret = 0, pok = 0, pitch_m = 0;
    int ok = 0;
    int ctM = 0, ctMok = 0, ctA = 0, ctMr = 0, dumpr = 0;
    int t20 = 0, t24 = 0, t28 = 0;
    int f2c = -1, f2d = -1, f2e = -1;
    int sr_reads = 0, sr_filt = 0, sr_fail = 0, sr_bh = 0;
    const char *xf = "none";
    char path[24];
    char ctMbuf[16];
    char pitchbuf[16];
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
        {
            /* v14 read-only verification: read the BGM channel's current pitch through the
             * same wrapper family the future rate entry will use; result code logged. */
            typedef int (*getpitch_fn)(void *, float *);
            getpitch_fn pf = (getpitch_fn)(uintptr_t)(g_base + OFF_GETPITCH);
            float f = -1.0f;
            pret = pf((void *)(uintptr_t)cs.chan0, &f);
            pok = (pret == 0);
            pitch_m = pok ? (int)(f * 1000.0f + (f >= 0.0f ? 0.5f : -0.5f)) : 0;
        }
    }
    /* S2b probes: checked safe reads only; the whole path is skipped without ident/mach */
    if (g_ident_ok && g_safe_ok) {
        g_sr_budget = SR_BUDGET_PER_CYCLE;
        g_sr_reads = 0;
        g_sr_filtered = 0;
        g_sr_failed = 0;
        g_sr_budget_hit = 0;
        ctMr = safe_read(g_base + OFF_CT_MIRROR, &ctM, 4);
        if (ctMr && ctM > -(1 << 26) && ctM < (1 << 26)) {
            ctMok = 1;
        }
        dumpr = safe_read(g_base + OFF_CT_DUMP_BASE, words, sizeof(words));
        if (!ctMr) {
            xf = "mirror";
        } else if (!dumpr) {
            xf = "dump";
        }
        if (cs.state == CH_READY) {
            ct_probe(cs.gg, &tl, &ctA, &t20, &t24, &t28, &f2c, &f2d, &f2e,
                     path, sizeof(path), &xf, (uint64_t)task->q_seq);
        }
        sr_reads = g_sr_reads;
        sr_filt = g_sr_filtered;
        sr_fail = g_sr_failed;
        sr_bh = g_sr_budget_hit;
    } else {
        xf = g_safe_ok ? "ident" : "nomach";
    }
    now = monotonic_ms();
    pthread_threadid_np(NULL, &tid);
    if (ctMr) {
        snprintf(ctMbuf, sizeof(ctMbuf), "%d", ctM);
    } else {
        snprintf(ctMbuf, sizeof(ctMbuf), "na");
    }
    if (pok) {
        snprintf(pitchbuf, sizeof(pitchbuf), "%d", pitch_m);
    } else {
        snprintf(pitchbuf, sizeof(pitchbuf), "na");
    }
    n = snprintf(line, sizeof(line),
                 "p seq=%llu t_ms=%llu t_q=%llu lag_ms=%llu tid=%llu main=%d am=%llx handle=%llx ok=%d pos_ms=%d"
                 " pitch=%s pchret=%d safe=%d reads=%d/%d/%d/%d cw=%d cD=%d ctA=%d ctM=%s ctMok=%d xf=%s",
                 (unsigned long long)task->q_seq, (unsigned long long)now,
                 (unsigned long long)task->q_t_ms, (unsigned long long)(now - task->q_t_ms),
                 (unsigned long long)tid, pthread_main_np() ? 1 : 0,
                 (unsigned long long)cs.am, (unsigned long long)cs.chan0, ok, pos,
                 pitchbuf, pret,
                 g_safe_ok ? 1 : 0, sr_reads, sr_filt, sr_fail, sr_bh,
                 g_crawl_enabled ? 1 : 0, g_hit_valid ? 1 : 0, ctA, ctMbuf, ctMok, xf);
    if (ctA && n > 0 && (size_t)n < sizeof(line) - 128) {
        n += snprintf(line + n, sizeof(line) - (size_t)n,
                      " path=%s tl=%llx t20=%d t24=%d t28=%d f2c=%d f2d=%d f2e=%d",
                      path, (unsigned long long)tl, t20, t24, t28, f2c, f2d, f2e);
    }
    if (dumpr && n > 0 && (size_t)n < sizeof(line) - 160) {
        int i;
        n += snprintf(line + n, sizeof(line) - (size_t)n, " w=");
        for (i = 0; i < CT_DUMP_WORDS; i++) {
            n += snprintf(line + n, sizeof(line) - (size_t)n, "%s%08x",
                          i ? " " : "", (unsigned)words[i]);
        }
    } else if (!dumpr && n > 0 && (size_t)n < sizeof(line) - 8) {
        n += snprintf(line + n, sizeof(line) - (size_t)n, " w=na");
    }
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
    saferead_init();
    crawl_init();

    if (gmtime_r(&now, &tmv) != NULL) {
        n = snprintf(header, sizeof(header),
                     "# practice_clock_probe v7 (S3a read-only verification: rate-entry probe)\n"
                     "# build=%s pid=%ld utc=%04d-%02d-%02dT%02d:%02d:%02dZ t0_ms=%llu\n",
                     PRACTICE_CLOCK_PROBE_VERSION, (long)getpid(),
                     tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                     tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                     (unsigned long long)t0);
    } else {
        n = snprintf(header, sizeof(header),
                     "# practice_clock_probe v7 (S3a read-only verification: rate-entry probe)\n"
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

    n = snprintf(header, sizeof(header), "# saferead=%s\n",
                 g_safe_ok ? "mach-vm/1" : "off/0");
    if (n > 0 && (size_t)n < sizeof(header)) {
        append_raw(header, (size_t)n);
    }

    n = snprintf(header, sizeof(header), "# crawl=%s\n",
                 g_crawl_enabled ? "on" : "off");
    if (n > 0 && (size_t)n < sizeof(header)) {
        append_raw(header, (size_t)n);
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
