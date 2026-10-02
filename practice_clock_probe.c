/* practice_clock_probe.c - S3a: anchor/units probe (v11).
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
 *     v14b (review fix): the returned float is checked (finite, |f| <= 1e5) BEFORE any
 *     use; the value is logged with 4 decimals; failures log explicit reasons
 *     (pcherr=ret|notfinite|range). No unchecked float-to-int conversion exists.
 *   - v15 (S3a session chain): the static route to the ACTIVE game session is found and
 *     probed read-only: singleton(0x1660280) -> +0x148 (registered session/wrapper) ->
 *     session (vtable marker 0x1521dc0) -> +0x3a0 -> scene (marker 0x14c0d08) -> +0x30 ->
 *     timeline (marker 0x14d0548). If the registered object is a wrapper, a bounded
 *     marker-driven field scan (0x00..0x380) locates the session pointer once (offset
 *     cached). Session/scene/flag reads are logged; NO writes. See
 *     docs/s3a_control_interface_notes.md.
 *   - v16 (S3a resolver diagnostics): the v15 single-hop probe is generalized into a
 *     bounded resolver - it logs RAW diagnostic values (singleton, sing+0x148, gg,
 *     gg+0x88 + its vtable, sing+0x140 + its vtable) and searches (one-shot per round,
 *     cached per class, marker-driven, with backoff) three windows (singleton
 *     0x00..0x1f8, GameGlobal 0x00..0xf0, gg+0x88 object 0x00..0x100) for pointers
 *     carrying the session vtable, the wrapper vtable, the scene marker or the timeline
 *     marker; a found wrapper additionally gets the bounded field scan (0x00..0x380) for
 *     the session pointer. Every resolved route is re-validated every cycle. NO writes.
 *     See docs/s3a_control_interface_notes.md.
 *   - v17 (S3a state-route probe): the v16 run resolved the live route ([singleton+0x140]
 *     -> session; wrapper+0x2b0 -> session). v17 adds a per-cycle quick candidate on that
 *     slot (immediate first discovery, no scan latency), plus state detail for the
 *     pause/resume work: timeline anchor pair (tl+0x10/+0x18), tl+0x34 (field34),
 *     tl+0x38 (resume-seek bias), scene+0x144 and session+0x2d0 state words. The bounded
 *     marker scans stay as fallback. NO writes.
 *   - v18 (S3a anchor/units probe): the v17 run decoded the pause/resume mechanics and
 *     showed t18 = wall ms (scene anchor). v18 logs the mach timebase (numer/denom) and
 *     mach_absolute_time per line to close the remaining unit questions, and re-reads the
 *     scene windows (+0x138 bytes: sf/s13b/s13e; +0x140 u64: s140/sc144), plus f2f.
 *     NO writes.
 *
 * Log: <sandbox>/Documents/practice_clock_probe.log (append-only).
 *   Header:  # practice_clock_probe v11 (S3a: anchor/units probe)
 *            # build=<id> pid=<n> utc=<ISO8601Z> t0_ms=<n>
 *   Ident:   # ident base=0x.. slide=0x.. img=.. magic=<0|1> words=<0|1> vt=<0|1> ...
 *   Reads:   # reads mainhop=<cf-runloop|unavailable>
 *   Saferead:# saferead=<mach-vm/1|off/0>
 *   Timebase:# timebase numer=<n> denom=<d>
 *   Sample:  s seq=<n> t_ms=<n> dt_ms=<n> [gap=1]
 *   Chain:   c seq=<n> t_ms=<n> tid=<n> main=<0|1> state=<ready|not-ready|anomaly|changed>
 *            [slot=0x.. gg=0x.. am=0x.. pv=0x.. count=<n> chan0=0x..] [reason=<..>]
 *   Position: p seq=<n> t_ms=<n> t_q=<n> lag_ms=<n> tid=<n> main=<0|1> am=0x.. handle=0x..
 *            ok=<0|1> pos_ms=<n> pitch=<f.ffff|na> pchret=<n> pcherr=<token> safe=<0|1>
 *            reads=<n>/<n>/<n>/<n> cD=<0|1> via=<token> soff=0x<off> sf=<n|-1> s470=<n|-1>
 *            s474=<n> t30=<n> mb=0x.. ctA=<0|1> ctM=<n|na> ctMok=<0|1> xf=<token>
 *            s1=0x.. ho=0x.. g0=0x.. g88=0x.. g88v=0x.. h140=0x.. v140=0x..
 *            t34=<n> f2f=<n> t10=0x.. t18=0x.. t38=0x.. sc144=<n> s140=<n> s13b=<n>
 *            s13e=<n> s2d0=<n>
 *            [sess=0x.. sc=0x.. tl=0x.. t20=<n> t24=<n> t28=<n> f2c=<n> f2d=<n> f2e=<n>]
 *            w=<16 x %08x|na> [reason=<..>]
 *            -- main-thread executed (main=1 expected); pitch = BGM channel pitch (float,
 *            4 decimals; checked finite and in range BEFORE use; na when invalid) with
 *            pchret = FMOD result code and pcherr = failure reason (none|ret|notfinite|
 *            range); safe = mach safe reads available; reads = safe-read attempts/filtered/
 *            failed/budget-hit this cycle; cD = a validated route to the timeline resolved
 *            this cycle; via = route tag (s1|gg|g8 + field offset; sc = scene-class only;
 *            tl = timeline-class only); soff = cached wrapper->session field offset;
 *            sf = scene pause/finish flag byte, s470 = session pause byte, s474 = session
 *            resume value, t30 = timeline bound (-1/0 when unreadable); ctA = timeline
 *            fields read this cycle; ctM = BSS mirror - <n> only when its checked read
 *            succeeded, else na; xf = last failure token (none|mirror|dump|sess|fields|
 *            ident|nomach); s1..v140 = raw diagnostic values (singleton; [sing+0x148];
 *            gg; [gg+0x88]; its vtable; [sing+0x140]; its vtable); t10/t18 = the timeline
 *            anchor pair, t34 = field34 (the >0?0:-3000 switch), t38 = the resume-seek
 *            bias, f2f = timeline flag byte 7, sc144/s140 = scene state words, s13b/s13e =
 *            scene flag bytes, s2d0 = session state word (pause-gate), mb = mach_absolute_time
 *            at line build; w = 16 u32 words at 0x16539a0 (mirror at index 8), or na.
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
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include "practice_obs_queue.h"

#ifndef PRACTICE_BUILD_ID
#define PRACTICE_BUILD_ID "dev"
#endif
#define PRACTICE_CLOCK_PROBE_VERSION "s3a-anchor-" PRACTICE_BUILD_ID

#define SAMPLE_INTERVAL_MS 1000
#define GAP_FACTOR 3
#define FLUSH_EVERY_LINES 8
#define LINEBUF 1280
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

/* v15 session chain + chart-time probes (see docs/s3a_control_interface_notes.md). */
#define OFF_CT_MIRROR         0x16539c0ULL  /* BSS mirror: last computed chart time    */
#define OFF_CT_DUMP_BASE      0x16539a0ULL  /* 16 u32 words around the mirror          */
#define OFF_SINGLETON         0x1660280ULL  /* app singleton global                    */
#define OFF_SING_OBJ          0x148ULL      /* singleton -> registered session/wrapper */
#define OFF_SING_H140         0x140ULL      /* singleton -> transition object (diagnostic)*/
#define OFF_GG_O88            0x88ULL       /* GameGlobal -> service object (diagnostic)  */
#define SESS_VT_OFF           0x1521dc0ULL  /* GameSession vtable (ctor 0xca4624)      */
#define WRAP_VT_OFF           0x14cbcc8ULL  /* wrapper vtable (0x388 object family)    */
#define OFF_SESS_SCENE        0x3a0ULL      /* session -> scene                        */
#define OFF_SESS_470          0x470ULL      /* session: pause byte + resume value      */
#define OFF_SCENE_TL          0x30ULL       /* scene -> GameTimeline                   */
#define OFF_SCENE_SF          0x138ULL      /* scene: pause/finished flag byte         */
#define SCENE_MARKER_OFF      0x14c0d08ULL  /* *(void**)scene == base + this (factory) */
#define TL_MARKER_OFF         0x14d0548ULL  /* *(void**)tl == base + this (factory)    */
#define OFF_TL_TIME           0x20ULL       /* timeline: (t20,t24) word                */
#define OFF_TL_W28            0x28ULL       /* timeline: (t28,f2c,f2d,f2e) word        */
#define OFF_TL_BOUND          0x30ULL       /* timeline: bound value                   */
#define OFF_TL_A10            0x10ULL       /* timeline anchor pair low                */
#define OFF_TL_A18            0x18ULL       /* timeline anchor pair high               */
#define OFF_TL_W38            0x38ULL       /* timeline resume-seek bias               */
#define OFF_SCENE_W140        0x140ULL      /* scene: state-word window (+0x140/+0x144)*/
#define OFF_SESS_2D0          0x2d0ULL      /* session state word (pause-gate)         */
#define OFF_WRAP_SESS         0x2b0ULL      /* wrapper -> session field (run 10-02)    */
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

/* ---- v19-obs: static-entry-trampoline observation (read-only wrt game state) ----
 * The main binary carries a pre-install patch: updateTime's first instruction is a
 * branch to a trampoline that calls pcp_obs_entry(tl, caller_lr) when the SLOT below
 * holds this function's address (written by the ctor). All runtime writes are confined
 * to the slot. The ring is written by the observed thread(s) and drained on the main
 * thread (same thread that owns the p-line output). */
/* ---- v20-obs: trampoline observation via the lead's observation queue ----------------
 * Callback fills a local PoqRecord and pushes it (try-once; drops are counted, never
 * spun or overwritten). On-site fields (t20/t28/state bytes) are read at the hook site
 * with validity bits; nothing is re-read later to "reconstruct" the event. */
#define OBS_SLOT_MAGIC   0x5043504F4253316bull   /* "PCPOBS1k" */
#define OBS_VALID_READS  0x1u   /* on-site timeline reads succeeded      */
#define OBS_VALID_MAIN   0x4u   /* pthread_main_np() true at the site    */
static PracticeObsQueue g_obs_q = POQ_INITIALIZER;
static uint64_t g_main_base;
static uint32_t g_tb_numer = 125, g_tb_denom = 3;   /* replaced by ctor probe */

__attribute__((used, noinline))
void pcp_obs_entry(uint64_t a, uint64_t lr, uint64_t tag)
{
    PoqRecord r;
    uint32_t validbits = 0;
    r.mach = mach_absolute_time();
    r.tag = tag;
    r.object = a;
    r.lr = lr;
    r.thread = (uint64_t)(uintptr_t)pthread_self();
    r.invocation = 0;                 /* pairing not established yet      */
    r.t20 = 0;
    r.t28 = 0;
    r.state = 0;
    {
        uint64_t tl = 0;
        if (tag == 1) {
            tl = a;                   /* entry hook: x0 = timeline        */
        } else if (tag == 2) {
            if ((a & 7ull) == 0 && (a - 0x100000000ull) < 0x100000000ull) {
                tl = *(volatile uint64_t *)(uintptr_t)(a + 0x30ull);   /* scene+0x30 */
            }
        }
        if (tl != 0 && (tl & 7ull) == 0 && (tl - 0x100000000ull) < 0x100000000ull) {
            uint64_t w20 = *(volatile uint64_t *)(uintptr_t)(tl + 0x20ull);
            uint64_t w28 = *(volatile uint64_t *)(uintptr_t)(tl + 0x28ull);
            uint32_t f2x = *(volatile uint32_t *)(uintptr_t)(tl + 0x2cull);
            r.t20 = (int32_t)(uint32_t)w20;
            r.t28 = (int32_t)(uint32_t)w28;
            r.state = (uint32_t)(f2x & 0xffffu);   /* +0x2c / +0x2d bytes */
            validbits |= OBS_VALID_READS;
        }
    }
    if (pthread_main_np()) {
        validbits |= OBS_VALID_MAIN;
    }
    r.valid = validbits;
    (void)poq_push(&g_obs_q, &r);
}

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

static int try_read32(uint64_t addr, uint32_t *out)
{
    if ((addr & 3ull) != 0 || (addr - 0x100000000ull) >= 0x100000000ull) {
        g_sr_filtered += 1;
        return 0;
    }
    return safe_read(addr, out, 4);
}

/* v16 resolver: routes to the ACTIVE gameplay session/scene/timeline.
 * Static map (see docs/s3a_control_interface_notes.md): singleton getter fn 0xe554c0
 * lazily creates the root object (size 0x208) and caches it at slot 0x1660280;
 * GameSession (ctor 0xca4624, vtable 0x1521dc0, size 0x4a0) is created, wrapped and
 * registered into [singleton+0x148]; scene (marker 0x14c0d08) hangs off [session+0x3a0];
 * timeline (marker 0x14d0548) off [scene+0x30]. The registered slot was EMPTY on the
 * first device run (v15); v16 therefore logs raw values and searches, one-shot and
 * marker-driven, bounded windows of the singleton, the GameGlobal and the gg+0x88
 * object for pointers carrying one of four markers (session vt, wrapper vt, scene,
 * timeline). Found offsets are cached per class (with a source tag), re-validated
 * every cycle, and cleared after repeated invalidation so a later rescan can refind.
 * All reads go through the checked safe-read interface; NO writes anywhere. */
static uint64_t g_sess_off;      /* wrapper -> session field offset (0 = unknown)       */
static int g_has_soff;           /* a wrapper->session offset is cached                 */
static uint64_t g_off_sess;      /* class: session-vt pointer                           */
static int g_src_sess;           /* 1 sing, 2 gg, 3 gg+0x88                             */
static int g_has_sess;
static uint64_t g_off_wrap;      /* class: wrapper-vt pointer                           */
static int g_src_wrap;
static int g_has_wrap;
static uint64_t g_off_scene;     /* class: scene marker pointer                         */
static int g_src_scene;
static int g_has_scene;
static uint64_t g_off_tl;        /* class: timeline marker pointer                      */
static int g_src_tl;
static int g_has_tl;
static int g_inval_sess, g_inval_scene, g_inval_tl;

/* discovery scheduling: bounded one-shot rounds with backoff while unresolved */
#define DISC_BASE_INTERVAL 30
static uint32_t g_disc_fail;
static uint64_t g_disc_next_seq;

typedef struct raw_diag {
    uint64_t s1, ho, g0, g88, g88v, h140, v140;
} raw_diag_t;

typedef struct tl_extra {
    uint64_t t10, t18, t38;   /* timeline anchor pair + resume-seek bias            */
    int t34;                  /* timeline field34 (the >0 ? 0 : -3000 switch)       */
    int f2f;                  /* timeline flag byte 7                               */
    int sc144;                /* scene state word (+0x144)                          */
    int s140;                 /* scene state word (+0x140)                          */
    int s13b, s13e;           /* scene flag bytes (+0x13b / +0x13e)                 */
    int s2d0;                 /* session state word (pause-gate)                    */
} tl_extra_t;

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
                          int *f2c, int *f2d, int *f2e, int *f2f)
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
    *f2f = (int)((w28 >> 56) & 0xffull);
    return 1;
}

static int is_sess(uint64_t p)
{
    uint64_t v = 0;
    return p != 0 && try_read64(p, &v) && v == g_base + SESS_VT_OFF;
}

static int is_wrap(uint64_t p)
{
    uint64_t v = 0;
    return p != 0 && try_read64(p, &v) && v == g_base + WRAP_VT_OFF;
}

static const char *src_tag(int src)
{
    return src == 1 ? "s1" : (src == 2 ? "gg" : (src == 3 ? "g8" : "??"));
}

/* Base object for a class source tag (re-read from roots every call). */
static uint64_t src_base(int src, uint64_t sing)
{
    uint64_t gg = 0, o = 0;
    if (src == 1) {
        return sing;
    }
    if (!safe_read(g_base + OFF_SLOT, &gg, 8) || !ptr_range_ok(gg)) {
        return 0;
    }
    if (src == 2) {
        return gg;
    }
    if (src == 3) {
        if (!try_read64(gg + OFF_GG_O88, &o) || !ptr_range_ok(o)) {
            return 0;
        }
        return o;
    }
    return 0;
}

/* One bounded window pass: first hit per class wins. Marker-driven only. */
static void marker_scan(uint64_t obj, int src, uint64_t limit)
{
    uint64_t off, v = 0, t = 0;
    if (!ptr_range_ok(obj)) {
        return;
    }
    for (off = 0; off <= limit; off += 8) {
        if (!try_read64(obj + off, &v) || v == 0 || !ptr_range_ok(v)) {
            continue;
        }
        if (!try_read64(v, &t)) {
            continue;
        }
        if (!g_has_sess && t == g_base + SESS_VT_OFF) {
            g_has_sess = 1; g_off_sess = off; g_src_sess = src;
        } else if (!g_has_wrap && t == g_base + WRAP_VT_OFF) {
            g_has_wrap = 1; g_off_wrap = off; g_src_wrap = src;
        } else if (!g_has_scene && t == g_base + SCENE_MARKER_OFF) {
            g_has_scene = 1; g_off_scene = off; g_src_scene = src;
        } else if (!g_has_tl && t == g_base + TL_MARKER_OFF) {
            g_has_tl = 1; g_off_tl = off; g_src_tl = src;
        }
    }
}

/* One bounded discovery round over the three windows (+ wrapper field scan). */
static void scan_round(uint64_t sing)
{
    uint64_t gg = 0, g88 = 0, w = 0, v = 0, t = 0, off;
    marker_scan(sing, 1, 0x1f8);
    if (safe_read(g_base + OFF_SLOT, &gg, 8) && ptr_range_ok(gg)) {
        marker_scan(gg, 2, 0xf0);
        if (try_read64(gg + OFF_GG_O88, &g88) && ptr_range_ok(g88)) {
            marker_scan(g88, 3, 0x100);
        }
    }
    if (g_has_wrap && !g_has_soff) {
        uint64_t b = src_base(g_src_wrap, sing);
        if (b && try_read64(b + g_off_wrap, &w) && is_wrap(w)) {
            for (off = 0; off <= 0x380; off += 8) {
                if (try_read64(w + off, &v) && v != 0 && ptr_range_ok(v) &&
                    try_read64(v, &t) && t == g_base + SESS_VT_OFF) {
                    g_has_soff = 1;
                    g_sess_off = off;
                    break;
                }
            }
        }
    }
}

/* Cached-class lookups. A class that keeps failing is dropped for a rescan. */
static void pick_classes(uint64_t sing, uint64_t *sess, uint64_t *scene, uint64_t *tl)
{
    uint64_t b = 0, v = 0, s2 = 0, t = 0;
    *sess = 0; *scene = 0; *tl = 0;
    if (g_has_sess) {
        b = src_base(g_src_sess, sing);
        if (b && try_read64(b + g_off_sess, &v) && is_sess(v)) {
            *sess = v; g_inval_sess = 0;
        } else if (++g_inval_sess >= 8) {
            g_has_sess = 0; g_inval_sess = 0;
        }
    }
    if (!*sess && g_has_wrap && g_has_soff) {
        b = src_base(g_src_wrap, sing);
        if (b && try_read64(b + g_off_wrap, &v) && is_wrap(v) &&
            try_read64(v + g_sess_off, &s2) && is_sess(s2)) {
            *sess = s2;
        }
    }
    if (g_has_scene) {
        b = src_base(g_src_scene, sing);
        if (b && try_read64(b + g_off_scene, &v) && is_scene(v)) {
            *scene = v; g_inval_scene = 0;
        } else if (++g_inval_scene >= 8) {
            g_has_scene = 0; g_inval_scene = 0;
        }
    }
    if (g_has_tl) {
        b = src_base(g_src_tl, sing);
        if (b && try_read64(b + g_off_tl, &v) && is_tl(v, &t)) {
            *tl = t; g_inval_tl = 0;
        } else if (++g_inval_tl >= 8) {
            g_has_tl = 0; g_inval_tl = 0;
        }
    }
    if (!*scene && *sess) {
        if (try_read64(*sess + OFF_SESS_SCENE, &v) && is_scene(v)) {
            *scene = v;
        }
    }
    if (!*tl && *scene) {
        if (try_read64(*scene + OFF_SCENE_TL, &v) && is_tl(v, tl)) {
            *tl = v;
        }
    }
}

/* Quick candidate: the [singleton+0x140] slot re-checked EVERY cycle (v16 run showed it
 * holds the live session in gameplay; the wrapper at transitions - wrapper+0x2b0 -> session).
 * Found objects are folded into the class caches so the normal validation path uses them. */
static void quick_candidates(uint64_t sing)
{
    uint64_t v = 0, w = 0;
    if (!ptr_range_ok(sing)) {
        return;
    }
    if (!try_read64(sing + OFF_SING_H140, &v) || v == 0 || !ptr_range_ok(v)) {
        return;
    }
    if (is_sess(v)) {
        if (!g_has_sess || g_off_sess != OFF_SING_H140) {
            g_has_sess = 1;
            g_off_sess = OFF_SING_H140;
            g_src_sess = 1;
        }
        g_inval_sess = 0;
        return;
    }
    if (is_wrap(v)) {
        if (!g_has_wrap || g_off_wrap != OFF_SING_H140) {
            g_has_wrap = 1;
            g_off_wrap = OFF_SING_H140;
            g_src_wrap = 1;
        }
        if (try_read64(v + OFF_WRAP_SESS, &w) && is_sess(w)) {
            if (!g_has_soff) {
                g_has_soff = 1;
                g_sess_off = OFF_WRAP_SESS;
            }
        }
    }
}

/* Resolve the best route this cycle; returns 1 when a timeline is available. */
static int resolve_chain(uint64_t sing, uint64_t *sess, uint64_t *scene, uint64_t *tl,
                         char *via, size_t vialen, uint64_t seq)
{
    *via = '\0';
    quick_candidates(sing);
    pick_classes(sing, sess, scene, tl);
    if (!*tl && seq >= g_disc_next_seq) {
        scan_round(sing);
        pick_classes(sing, sess, scene, tl);
        if (*tl) {
            g_disc_fail = 0;
            g_disc_next_seq = seq + DISC_BASE_INTERVAL;
        } else {
            if (g_disc_fail < 6) {
                g_disc_fail += 1;
            }
            g_disc_next_seq = seq + (uint64_t)(DISC_BASE_INTERVAL << g_disc_fail);
        }
    }
    if (!*tl) {
        return 0;
    }
    if (g_has_sess && *sess) {
        snprintf(via, vialen, "%s+%x", src_tag(g_src_sess), (unsigned)g_off_sess);
    } else if (g_has_wrap && g_has_soff && *sess) {
        snprintf(via, vialen, "wr.+%x/%x", (unsigned)g_off_wrap, (unsigned)g_sess_off);
    } else if (*scene) {
        snprintf(via, vialen, "sc");
    } else {
        snprintf(via, vialen, "tl");
    }
    return 1;
}

/* Runs on the main thread; every read goes through the checked safe-read interface. */
static void ct_probe(uint64_t *tl, int *ctCd, int *ctA, int *t20, int *t24, int *t28,
                     int *f2c, int *f2d, int *f2e, int *sf, int *s470, int *s474, int *t30,
                     uint64_t *sess_out, uint64_t *scene_out, const char **xf,
                     uint64_t seq, char *via, size_t vialen, raw_diag_t *dg, tl_extra_t *ex)
{
    uint64_t sing = 0, gg = 0, g88 = 0, v = 0;
    uint64_t sess = 0, scene = 0;
    *ctCd = 0;
    *ctA = 0;
    *sf = -1;
    *s470 = -1;
    *s474 = 0;
    *t30 = 0;
    *sess_out = 0;
    *scene_out = 0;
    via[0] = '\0';
    memset(dg, 0, sizeof(*dg));
    memset(ex, 0, sizeof(*ex));
    if (!g_safe_ok) {
        return;
    }
    if (safe_read(g_base + OFF_SINGLETON, &sing, 8) && ptr_range_ok(sing)) {
        dg->s1 = sing;
        (void)try_read64(sing + OFF_SING_OBJ, &dg->ho);
        if (try_read64(sing + OFF_SING_H140, &dg->h140) && ptr_range_ok(dg->h140)) {
            (void)try_read64(dg->h140, &dg->v140);
        }
    }
    if (safe_read(g_base + OFF_SLOT, &gg, 8) && ptr_range_ok(gg)) {
        dg->g0 = gg;
        if (try_read64(gg + OFF_GG_O88, &g88) && ptr_range_ok(g88)) {
            dg->g88 = g88;
            (void)try_read64(g88, &dg->g88v);
        }
    }
    if (!resolve_chain(sing, &sess, &scene, tl, via, vialen, seq)) {
        *xf = "sess";
        return;
    }
    *ctCd = 1;
    *sess_out = sess;
    *scene_out = scene;
    if (scene != 0 && try_read64(scene + OFF_SCENE_SF, &v)) {
        *sf = (int)(v & 0xffull);
        ex->s13b = (int)((v >> 24) & 0xffull);
        ex->s13e = (int)((v >> 48) & 0xffull);
    }
    if (sess != 0 && try_read64(sess + OFF_SESS_470, &v)) {
        *s470 = (int)(v & 0xffull);
        *s474 = (int)(uint32_t)(v >> 32);
    }
    if (try_read64(*tl + OFF_TL_BOUND, &v)) {
        *t30 = (int)(uint32_t)v;
        ex->t34 = (int)(uint32_t)(v >> 32);
    }
    if (try_read64(*tl + OFF_TL_A10, &v)) {
        ex->t10 = v;
    }
    if (try_read64(*tl + OFF_TL_A18, &v)) {
        ex->t18 = v;
    }
    if (try_read64(*tl + OFF_TL_W38, &v)) {
        ex->t38 = v;
    }
    if (scene != 0 && try_read64(scene + OFF_SCENE_W140, &v)) {
        ex->s140 = (int)(uint32_t)v;
        ex->sc144 = (int)(uint32_t)(v >> 32);
    }
    if (sess != 0 && try_read64(sess + OFF_SESS_2D0, &v)) {
        ex->s2d0 = (int)(uint32_t)v;
    }
    {
        int f2f = -1;
        if (!tl_read_fields(*tl, t20, t24, t28, f2c, f2d, f2e, &f2f)) {
            *xf = "fields";
            return;
        }
        ex->f2f = f2f;
    }
    *ctA = 1;
    *xf = "none";
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
    float pfv = -1.0f;
    int pret = 0, pok = 0;
    const char *pitch_why = "none";
    int ok = 0;
    int ctM = 0, ctMok = 0, ctA = 0, ctMr = 0, dumpr = 0, ctCd = 0;
    int sf = -1, s470 = -1, s474 = 0, t30 = 0;
    int t20 = 0, t24 = 0, t28 = 0;
    int f2c = -1, f2d = -1, f2e = -1;
    int sr_reads = 0, sr_filt = 0, sr_fail = 0, sr_bh = 0;
    int uc = 0, uv = 0, upm = 0, unm = 0, ub1 = 0, ub2 = 0, urvd = 0, us = 0;
    unsigned int umin = 0, umax = 0;
    unsigned int uq[5] = {0, 0, 0, 0, 0};
    uint64_t urv = 0;
    int ut20 = 0, ut28 = 0;
    unsigned int ust = 0;
    int uh[6] = {0, 0, 0, 0, 0, 0};
    uint64_t sess_addr = 0, scene_addr = 0;
    uint64_t mach_now = 0;
    raw_diag_t dg;
    tl_extra_t te;
    const char *xf = "none";
    char via[24];
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
             * same wrapper family the future rate entry will use. The float value is checked
             * (finite, sane range) before any use; result code and failure reason are logged.
             * (v14b fix per the 2026-10-01 review: no unchecked float-to-int conversion.) */
            typedef int (*getpitch_fn)(void *, float *);
            getpitch_fn pf = (getpitch_fn)(uintptr_t)(g_base + OFF_GETPITCH);
            pret = pf((void *)(uintptr_t)cs.chan0, &pfv);
            if (pret != 0) {
                pitch_why = "ret";
            } else if (!isfinite(pfv)) {
                pitch_why = "notfinite";
            } else if (pfv < -1.0e5f || pfv > 1.0e5f) {
                pitch_why = "range";
            } else {
                pok = 1;
            }
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
            ct_probe(&tl, &ctCd, &ctA, &t20, &t24, &t28, &f2c, &f2d, &f2e,
                     &sf, &s470, &s474, &t30, &sess_addr, &scene_addr, &xf,
                     (uint64_t)task->q_seq, via, sizeof(via), &dg, &te);
        }
        sr_reads = g_sr_reads;
        sr_filt = g_sr_filtered;
        sr_fail = g_sr_failed;
        sr_bh = g_sr_budget_hit;
        /* v20-obs: bounded drain of the observation queue (try-once pops). */
        {
            uint64_t prev_mach = 0;
            uint64_t rva_slot[6];
            int slot_fill = 0;
            int j, k;
            PoqRecord rec;
            PoqStats st;
            for (j = 0; j < 6; j++) rva_slot[j] = 0;
            for (j = 0; j < 256; j++) {
                if (poq_pop(&g_obs_q, &rec) != POQ_OK) {
                    break;
                }
                uc++;
                if (rec.tag == 1) ub1++; else if (rec.tag == 2) ub2++;
                if (rec.valid & OBS_VALID_MAIN) upm++; else unm++;
                if (rec.valid & OBS_VALID_READS) uv++;
                ut20 = rec.t20; ut28 = rec.t28; ust = rec.state;
                if (rec.lr > g_main_base) {
                    uint64_t norm = rec.lr - g_main_base;
                    urv = norm;
                    for (k = 0; k < slot_fill; k++) {
                        if (rva_slot[k] == norm) break;
                    }
                    if (k == slot_fill && slot_fill < 6) {
                        rva_slot[slot_fill++] = norm;
                    }
                }
                if (uc > 1 && rec.mach > prev_mach) {
                    uint64_t gap_us = (rec.mach - prev_mach) * (uint64_t)g_tb_numer /
                                      ((uint64_t)g_tb_denom * 1000ull);
                    if (umin == 0 || gap_us < umin) umin = (unsigned int)gap_us;
                    if (gap_us > umax) umax = (unsigned int)gap_us;
                    if (gap_us < 100) uh[0]++;
                    else if (gap_us < 500) uh[1]++;
                    else if (gap_us < 1000) uh[2]++;
                    else if (gap_us < 2000) uh[3]++;
                    else if (gap_us < 5000) uh[4]++;
                    else uh[5]++;
                }
                prev_mach = rec.mach;
            }
            urvd = slot_fill;
            st = poq_stats(&g_obs_q);
            uq[0] = st.attempted; uq[1] = st.accepted; uq[2] = st.dropped_busy;
            uq[3] = st.dropped_full; uq[4] = st.drained;
            if (g_main_base != 0 &&
                *(volatile uint64_t *)(uintptr_t)(g_main_base + 0x165BE00ull) == OBS_SLOT_MAGIC) {
                us = 1;
            }
        }
    } else {
        xf = g_safe_ok ? "ident" : "nomach";
    }
    now = monotonic_ms();
    mach_now = mach_absolute_time();
    pthread_threadid_np(NULL, &tid);
    if (ctMr) {
        snprintf(ctMbuf, sizeof(ctMbuf), "%d", ctM);
    } else {
        snprintf(ctMbuf, sizeof(ctMbuf), "na");
    }
    if (pok) {
        snprintf(pitchbuf, sizeof(pitchbuf), "%0.4f", (double)pfv);
    } else {
        snprintf(pitchbuf, sizeof(pitchbuf), "na");
    }
    n = snprintf(line, sizeof(line),
                 "p seq=%llu t_ms=%llu t_q=%llu lag_ms=%llu tid=%llu main=%d am=%llx handle=%llx ok=%d pos_ms=%d"
                 " pitch=%s pchret=%d pcherr=%s safe=%d reads=%d/%d/%d/%d cD=%d via=%s soff=0x%x"
                 " sf=%d s470=%d s474=%d t30=%d ctA=%d ctM=%s ctMok=%d xf=%s mb=%llx"
                 " uc=%d uv=%d upm=%d unm=%d ub=%d/%d uh=%d/%d/%d/%d/%d/%d umin=%u umax=%u"
                 " urv=%llx urvd=%d ut=%d/%d/%x uq=%u/%u/%u/%u/%u us=%d",
                 (unsigned long long)task->q_seq, (unsigned long long)now,
                 (unsigned long long)task->q_t_ms, (unsigned long long)(now - task->q_t_ms),
                 (unsigned long long)tid, pthread_main_np() ? 1 : 0,
                 (unsigned long long)cs.am, (unsigned long long)cs.chan0, ok, pos,
                 pitchbuf, pret, pitch_why,
                 g_safe_ok ? 1 : 0, sr_reads, sr_filt, sr_fail, sr_bh,
                 ctCd, via[0] ? via : "-", (unsigned)g_sess_off,
                 sf, s470, s474, t30, ctA, ctMbuf, ctMok, xf,
                 (unsigned long long)mach_now,
                 uc, uv, upm, unm, ub1, ub2, uh[0], uh[1], uh[2], uh[3], uh[4], uh[5],
                 umin, umax, (unsigned long long)urv, urvd,
                 ut20, ut28, ust, uq[0], uq[1], uq[2], uq[3], uq[4], us);
    if (n > 0 && (size_t)n < sizeof(line) - 260) {
        n += snprintf(line + n, sizeof(line) - (size_t)n,
                      " s1=%llx ho=%llx g0=%llx g88=%llx g88v=%llx h140=%llx v140=%llx",
                      (unsigned long long)dg.s1, (unsigned long long)dg.ho,
                      (unsigned long long)dg.g0, (unsigned long long)dg.g88,
                      (unsigned long long)dg.g88v, (unsigned long long)dg.h140,
                      (unsigned long long)dg.v140);
    }
    if (n > 0 && (size_t)n < sizeof(line) - 190) {
        n += snprintf(line + n, sizeof(line) - (size_t)n,
                      " t34=%d f2f=%d t10=%llx t18=%llx t38=%llx sc144=%d s140=%d s13b=%d s13e=%d s2d0=%d",
                      te.t34, te.f2f, (unsigned long long)te.t10, (unsigned long long)te.t18,
                      (unsigned long long)te.t38, te.sc144, te.s140, te.s13b, te.s13e, te.s2d0);
    }
    if (ctA && n > 0 && (size_t)n < sizeof(line) - 128) {
        n += snprintf(line + n, sizeof(line) - (size_t)n,
                      " sess=%llx sc=%llx tl=%llx t20=%d t24=%d t28=%d f2c=%d f2d=%d f2e=%d",
                      (unsigned long long)sess_addr, (unsigned long long)scene_addr,
                      (unsigned long long)tl, t20, t24, t28, f2c, f2d, f2e);
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

    if (gmtime_r(&now, &tmv) != NULL) {
        n = snprintf(header, sizeof(header),
                     "# practice_clock_probe v11 (S3a: anchor/units probe)\n"
                     "# build=%s pid=%ld utc=%04d-%02d-%02dT%02d:%02d:%02dZ t0_ms=%llu\n",
                     PRACTICE_CLOCK_PROBE_VERSION, (long)getpid(),
                     tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                     tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                     (unsigned long long)t0);
    } else {
        n = snprintf(header, sizeof(header),
                     "# practice_clock_probe v11 (S3a: anchor/units probe)\n"
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

    {
        mach_timebase_info_data_t tbi;   /* distinct name: monotonic_ms has its own tb */
        if (mach_timebase_info(&tbi) == KERN_SUCCESS && tbi.denom != 0) {
            g_tb_numer = tbi.numer;
            g_tb_denom = tbi.denom;
            n = snprintf(header, sizeof(header), "# timebase numer=%u denom=%u\n",
                         (unsigned)tbi.numer, (unsigned)tbi.denom);
            if (n > 0 && (size_t)n < sizeof(header)) {
                append_raw(header, (size_t)n);
            }
        }
    }

    /* v20-obs: init the observation queue, then wire the trampoline slot. */
    {
        uintptr_t main_base = (uintptr_t)_dyld_get_image_header(0);
        int qok = poq_init(&g_obs_q);
        if (main_base != 0 && qok) {
            volatile uint64_t *magic = (volatile uint64_t *)(main_base + 0x165BE00ull);
            volatile uint64_t *ptr = (volatile uint64_t *)(main_base + 0x165BE08ull);
            uint64_t pre_magic = *magic;
            uint64_t pre_ptr = *ptr;
            if (pre_magic == 0 && pre_ptr == 0) {
                *ptr = (uint64_t)(uintptr_t)&pcp_obs_entry;
                *magic = OBS_SLOT_MAGIC;
                g_main_base = (uint64_t)main_base;
                n = snprintf(header, sizeof(header),
                             "# obs wired main=0x%llx slot=%016llx ptr=%p\n",
                             (unsigned long long)main_base,
                             (unsigned long long)OBS_SLOT_MAGIC,
                             (void *)&pcp_obs_entry);
            } else {
                n = snprintf(header, sizeof(header),
                             "# obs slot OCCUPIED pre=%016llx/%016llx - NOT wired\n",
                             (unsigned long long)pre_magic, (unsigned long long)pre_ptr);
            }
        } else {
            n = snprintf(header, sizeof(header),
                         "# obs NOT wired (main_base=%d qok=%d)\n", main_base != 0, qok);
        }
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
