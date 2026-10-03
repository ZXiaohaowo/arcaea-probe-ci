/* practice_clock_probe.c - v25 pause UI and sustained rate control.
 * Current behavior: W1 applies virtual-time bias; guarded setPitch controls BGM.
 * Scene destruction / BGM replacement restore audio before native teardown.
 * The following v9-v18 notes describe historical scopes, NOT current permissions.
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
#include "practice_seek.h"
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
#include "practice_shadow.h"
#include "practice_rate_session.h"
#include "practice_rate_ui.h"

#ifndef PRACTICE_BUILD_ID
#define PRACTICE_BUILD_ID "dev"
#endif
#define PRACTICE_CLOCK_PROBE_VERSION "ds4f-v38-" PRACTICE_BUILD_ID

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
#define OFF_SETPITCH          0x10e6458ULL
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
static PracticeShadow g_shadow;
static int g_safe_ok;
static unsigned int g_task_port;
static PracticeRateSession g_rate;
typedef struct { uint64_t am, pv, handle, scene, timeline; } rate_owner_t;
static rate_owner_t g_rate_owner;
static unsigned g_rate_requested = 75, g_rate_stable;
static uint64_t g_rate_candidate_tl, g_rate_candidate_handle;
static uint64_t g_rate_last_tl, g_rate_last_mach;
static int32_t g_rate_last_native;
static uint32_t g_rate_last_state;
static uint64_t g_rate_dtor_events, g_rate_play_events;
static _Atomic int g_rate_badthread;
/* v26: persistent setting + per-song session scope. */
static unsigned g_setting_percent = 100;
static uint64_t g_song_scene, g_song_timeline;
static int g_song_started, g_song_attempts;
/* Read-only UI probe (tag=6) results, written from the PauseLayer init hook. */
extern void ds4f_ui_probe_entry(uint64_t node, uint64_t layer);
extern volatile uint64_t g_uip_node, g_uip_layer, g_uip_child, g_uip_seq;
extern volatile int g_uip_slot_ok;
extern volatile int g_uip_install;
extern volatile uint64_t g_uip_practice, g_uip_fake;
static uint64_t g_uip_logged;
static uint32_t g_tb_numer, g_tb_denom; /* zero means unavailable */
static PracticeRateUIState g_ui={0,0,0,0,0,100,1,0};
static uint64_t g_ui_open_seq;
/* ds4f v27d: note scroll-speed mode + tag8 bridge state. The main-thread cycle
 * publishes the multiplier (100/rate, or 1.0 when sync/unpublished); the store
 * hook in LogicChart::update reads it through pcp_obs_entry(tag 8) and writes
 * the scaled value back to chart+0xf4. 0 scale bits means "not published yet"
 * and the hook is an identity then. */
static _Atomic unsigned g_note_mode;
static _Atomic uint32_t g_note_scale_bits;
static volatile uint64_t g_scroll_calls, g_scroll_prev_calls, g_scroll_chart;
static volatile uint32_t g_scroll_raw_bits, g_scroll_out_bits;
/* v27e: the game stores the scroll scalar only when the chart is (re)built
 * (log 22: 4 store events in 947 s, all at pos=0). The rate/mode correction
 * therefore has to be written by us; the tag8 bridge remains the observer and
 * keeps the identity return. */
static volatile int g_scroll_chart_valid;
static volatile uint32_t g_scroll_field_bits;   /* last value seen in chart+0xf4 */
static volatile uint64_t g_scroll_writes, g_scroll_write_fail;
static volatile uint64_t g_scroll_gen, g_scroll_gen_at_hook;
static volatile uint64_t g_scroll_vptr;        /* chart vptr captured at hook time */
/* v27f: call-site context for the chart's note-geometry rebuild (0x910CE0). */
static volatile uint64_t g_scroll_build_x1, g_scroll_build_x2;
static volatile uint32_t g_scroll_baked_scale_bits;  /* scale used at last build/relayout */
static volatile uint64_t g_scroll_relayouts, g_scroll_relayout_fail;
static volatile int g_scroll_relayout_blocked;
static void scroll_cache_clear(void)
{
    g_scroll_chart_valid = 0;
    g_scroll_chart = 0;
    g_scroll_raw_bits = 0;
    g_scroll_field_bits = 0;
    g_scroll_vptr = 0;
    g_scroll_build_x1 = 0;
    g_scroll_build_x2 = 0;
    g_scroll_baked_scale_bits = 0;
    g_scroll_relayout_blocked = 0;
}
void pcp_rate_ui_open(void)
{
    g_ui_open_seq++;
}
static uint64_t g_ui_seen_mach,g_ui_request_epoch;
static unsigned g_ui_request_percent;
PracticeRateUIState pcp_rate_ui_state(void)
{
    PracticeRateUIState s={0};
    uint64_t now=0,last=0;
    if (!pthread_main_np()) return s;
    s=g_ui;s.applied=g_rate.phase?g_rate.clock.percent:g_setting_percent;
    s.open_seq=g_ui_open_seq;
    if (atomic_load_explicit(&g_rate_badthread,memory_order_relaxed)) {s.ready=0;s.result=-3;}
    if (!ps_mach_us(mach_absolute_time(),g_tb_numer,g_tb_denom,&now) ||
        !ps_mach_us(g_ui_seen_mach,g_tb_numer,g_tb_denom,&last) || now<last || now-last>1500000 ||
        (g_rate_last_state & 0xffffu)!=0x101u) s.visible=s.ready=0;
    return s;
}
int pcp_rate_ui_request(unsigned percent,uint64_t epoch)
{
    PracticeRateUIState s=pcp_rate_ui_state();
    if (!pthread_main_np() || !s.visible || !s.ready || s.pending || epoch!=s.epoch || percent<PCP_RATE_MIN_PERCENT || percent>PCP_RATE_MAX_PERCENT) return 0;
    g_ui_request_percent=percent;g_ui_request_epoch=epoch;
    g_ui.pending=1;g_ui.result=0;
    return 1;
}

static int rate_read64(uint64_t addr, uint64_t *out)
{
    unsigned long long got=0;
    if (!g_safe_ok || (addr & 7) || addr<0x100000000ULL || addr>=0x200000000ULL) return 0;
    return mach_vm_read_overwrite(g_task_port,addr,8,(unsigned long long)(uintptr_t)out,&got)==0 && got==8;
}
static int rate_current(void *context)
{
    rate_owner_t *o=context;
    uint64_t gg,am,pv,vt,begin,end,handle;
    return rate_read64(g_base+OFF_SLOT,&gg) && rate_read64(gg+OFF_GG_AM,&am) && am==o->am &&
        rate_read64(am,&vt) && vt==g_base+OFF_AM_VT &&
        rate_read64(am+OFF_AM_PROVIDER,&pv) && pv==o->pv &&
        rate_read64(pv,&vt) && vt==g_base+OFF_PV_VT &&
        rate_read64(pv+OFF_PV_VEC_BEGIN,&begin) && rate_read64(pv+OFF_PV_VEC_END,&end) &&
        end>=begin && end-begin>=16 && end-begin<=CHAN_COUNT_CAP*16 && (end-begin)%16==0 &&
        rate_read64(begin+8,&handle) && handle==o->handle;
}
static int rate_get(void *context,float *value)
{
    rate_owner_t *o=context;
    typedef int (*fn)(void *,float *);
    return ((fn)(uintptr_t)(g_base+OFF_GETPITCH))((void *)(uintptr_t)o->handle,value);
}
static int rate_set(void *context,float value)
{
    rate_owner_t *o=context;
    typedef int (*fn)(void *,float);
    return ((fn)(uintptr_t)(g_base+OFF_SETPITCH))((void *)(uintptr_t)o->handle,value);
}

/* ds4f v28 stage 1: pitch-shift DSP lifecycle probe. Creates the built-in
 * pitch DSP through the FMOD system obtained from the channel, reads its info
 * and FFT parameter (latency estimate = fft/48 ms), then releases it. Nothing
 * is attached to the audio graph, so sound is untouched; this only proves the
 * addresses, ABI and create/release lifecycle before any attach experiment. */
#define OFF_CC_GETSYSTEMOBJ  0x10E5C90ULL
#define OFF_SYS_CREATEDSP    0x10AEBC4ULL
#define OFF_DSP_GETINFO      0x1067DD0ULL
#define OFF_DSP_GETPARAMF    0x1067370ULL
#define OFF_DSP_RELEASE      0x10657B8ULL
static int g_pitch_probe_done;
static volatile int g_pitch_hit = -1;
static volatile uint64_t g_pitch_sys, g_pitch_dsp;
static volatile int g_pitch_type = -1;
static volatile int g_pitch_rc_sys, g_pitch_rc_create, g_pitch_rc_info, g_pitch_rc_get, g_pitch_rc_rel;
static volatile float g_pitch_fft, g_pitch_lat_ms;
static char g_pitch_name[40];

static int pitch_probe_run(void *channel)
{
    typedef int32_t (*GetSysFn)(void *, void **);
    typedef int32_t (*CreateDspFn)(void *, int32_t, void **);
    typedef int32_t (*DspInfoFn)(void *, char *, uint32_t *, int32_t *, int32_t *, int32_t *);
    typedef int32_t (*DspGetParamFn)(void *, int32_t, float *, char *, int32_t);
    typedef int32_t (*DspReleaseFn)(void *);
    GetSysFn getsys=(GetSysFn)(uintptr_t)(g_base+OFF_CC_GETSYSTEMOBJ);
    CreateDspFn create=(CreateDspFn)(uintptr_t)(g_base+OFF_SYS_CREATEDSP);
    DspInfoFn info=(DspInfoFn)(uintptr_t)(g_base+OFF_DSP_GETINFO);
    DspGetParamFn getp=(DspGetParamFn)(uintptr_t)(g_base+OFF_DSP_GETPARAMF);
    DspReleaseFn release=(DspReleaseFn)(uintptr_t)(g_base+OFF_DSP_RELEASE);
    void *sys=NULL;
    int32_t t;
    g_pitch_probe_done=1;
    if (!channel || !g_base) return 0;
    g_pitch_rc_sys=getsys(channel,&sys);
    if (g_pitch_rc_sys!=0 || !sys) return 0;
    g_pitch_sys=(uint64_t)(uintptr_t)sys;
    for (t=9;t<=18;t++) {
        void *d=NULL;
        char name[40]={0};
        uint32_t ver=0;
        int32_t nch=0,cw=0,chh=0,rci,rcg,rcr;
        int32_t rc=create(sys,t,&d);
        if (rc!=0 || !d) { if (rc!=0) g_pitch_rc_create=rc; continue; }
        rci=info(d,name,&ver,&nch,&cw,&chh);
        if (rci==0 && strstr(name,"Pitch")) {
            float fft=0.0f;
            char vs[40]={0};
            rcg=getp(d,1,&fft,vs,sizeof(vs));
            g_pitch_hit=1; g_pitch_type=t; g_pitch_dsp=(uint64_t)(uintptr_t)d;
            g_pitch_rc_info=rci; g_pitch_rc_get=rcg;
            g_pitch_fft=(rcg==0)?fft:0.0f;
            g_pitch_lat_ms=(rcg==0 && fft>0.0f)?(fft/48.0f):0.0f;
            snprintf(g_pitch_name,sizeof(g_pitch_name),"%s",name);
            break;
        }
        rcr=release(d);
        if (rcr!=0) g_pitch_rc_rel=rcr;
    }
    if (g_pitch_type>=0 && g_pitch_dsp) {
        int32_t rcr=release((void *)(uintptr_t)g_pitch_dsp);
        if (rcr!=0) g_pitch_rc_rel=rcr;
    } else {
        g_pitch_hit=0;
    }
    return g_pitch_hit;
}

/* ds4f v29 stage 2: keep-pitch attach session. One DSP per process; attached
 * only while paused and a non-1x rate is applied; parameter 0 = 1/rate
 * (0.5..2.0 ratio semantics), read back after every write. On 1x / mode off /
 * channel change the DSP is detached and released. */
#define OFF_CC_ADDDSP      0x10E82F4ULL
#define OFF_CC_REMOVEDSP   0x10E8424ULL
#define OFF_DSP_SETPARAMF  0x1066ED4ULL
#define OFF_CC_GETPOS      0x1036D1CULL
#define OFF_CC_SETPOS      0x1036BF4ULL
static void *g_pd_dsp, *g_pd_chan;
static volatile int g_pd_attached;
static volatile int g_pd_rc_create, g_pd_rc_add, g_pd_rc_set, g_pd_rc_get, g_pd_rc_rm, g_pd_rc_rel;
static volatile float g_pd_ratio, g_pd_readback;
static volatile uint64_t g_pd_attach_events, g_pd_detach_events;
static volatile int g_pd_comp_ms, g_pd_last_comp, g_pd_rc_getpos, g_pd_rc_setpos;
static volatile uint32_t g_pd_pos_before, g_pd_pos_after, g_pd_bump;

static int32_t pd_setparam(void *d, float v)
{
    typedef int32_t (*fn_t)(void *, int32_t, float);
    return ((fn_t)(uintptr_t)(g_base+OFF_DSP_SETPARAMF))(d, 0, v);
}

static void *pd_apply(float ratio, void *chan)
{
    typedef int32_t (*AddFn)(void *, int32_t, void *);
    typedef int32_t (*GetParamFn)(void *, int32_t, float *, char *, int32_t);
    if (!chan) return NULL;
    if (!g_pd_dsp) {
        void *d=NULL;
        typedef int32_t (*CreateFn)(void *, int32_t, void **);
        if (!g_pitch_sys || g_pitch_type<0) return NULL;
        g_pd_rc_create=((CreateFn)(uintptr_t)(g_base+OFF_SYS_CREATEDSP))(
            (void *)(uintptr_t)g_pitch_sys, g_pitch_type, &d);
        if (g_pd_rc_create!=0 || !d) return NULL;
        g_pd_dsp=d;
    }
    g_pd_rc_set=pd_setparam(g_pd_dsp,ratio);
    {
        float v=0.0f;
        char vs[32]={0};
        g_pd_rc_get=((GetParamFn)(uintptr_t)(g_base+OFF_DSP_GETPARAMF))(
            g_pd_dsp,0,&v,vs,(int32_t)sizeof(vs));
        if (g_pd_rc_get==0) g_pd_readback=v;
    }
    g_pd_ratio=ratio;
    if (!g_pd_attached || g_pd_chan!=chan) {
        int attached_now=0;
        g_pd_rc_add=((AddFn)(uintptr_t)(g_base+OFF_CC_ADDDSP))(chan,0,g_pd_dsp);
        if (g_pd_rc_add==0) {
            g_pd_chan=chan;
            g_pd_attached=1;
            g_pd_attach_events++;
            attached_now=1;
        }
        /* v30: latency compensation, only on a fresh attach (never per set,
         * otherwise the offset would accumulate). bump = rate * comp_ms in
         * content time. */
        if (attached_now) {
            unsigned comp=pcp_pitch_comp_get();
            g_pd_comp_ms=(int)comp; g_pd_last_comp=(int)comp;
            if (comp) {
                typedef int32_t (*GetPosFn)(void *, uint32_t *, uint32_t);
                typedef int32_t (*SetPosFn)(void *, uint32_t, uint32_t);
                uint32_t pos=0;
                g_pd_rc_getpos=((GetPosFn)(uintptr_t)(g_base+OFF_CC_GETPOS))(chan,&pos,1);
                if (g_pd_rc_getpos==0) {
                    uint32_t bump=(uint32_t)((double)ratio*(double)comp+0.5);
                    g_pd_pos_before=pos; g_pd_bump=bump;
                    if (bump) {
                        g_pd_rc_setpos=((SetPosFn)(uintptr_t)(g_base+OFF_CC_SETPOS))(chan,pos+bump,1);
                        if (g_pd_rc_setpos==0) g_pd_pos_after=pos+bump;
                    }
                }
            }
        }
    }
    return g_pd_dsp;
}

static void pd_restore(void)
{
    typedef int32_t (*RemoveFn)(void *, void *);
    typedef int32_t (*ReleaseFn)(void *);
    if (!g_pd_dsp) return;
    (void)pd_setparam(g_pd_dsp,1.0f);
    if (g_pd_attached && g_pd_chan)
        g_pd_rc_rm=((RemoveFn)(uintptr_t)(g_base+OFF_CC_REMOVEDSP))(g_pd_chan,g_pd_dsp);
    g_pd_rc_rel=((ReleaseFn)(uintptr_t)(g_base+OFF_DSP_RELEASE))(g_pd_dsp);
    g_pd_dsp=NULL; g_pd_chan=NULL; g_pd_attached=0; g_pd_detach_events++;
}

/* ds4f v28 stage 1 (A/B stream): bookmark capture while paused. Positions are
 * source-time ms from the same audio read the p-line uses; the list is kept in
 * this process for now and every capture is logged. Seek/replay comes later. */
#define PCP_BM_MAX 32
static uint32_t g_bm_pos[PCP_BM_MAX];
static volatile uint64_t g_bm_count;
static volatile int g_last_pos_ms, g_last_pos_ok;

/* ds4f v32 stage A/B-1: A/B points (source ms + chart ms) and the seek
 * experiment. A jump moves the audio channel and rebases the W1 clock bias so
 * the consumer chart time lands on the captured chart value; note/judgement
 * state is NOT rebuilt yet (that is the S5 research item), so this is a
 * candidate experiment with rollback. */
static volatile uint32_t g_ab_pos[2];
static volatile int32_t g_ab_chart[2];
static volatile int g_ab_have[2];
static volatile int g_ab_loop;
static PracticeSeek g_seek;
static PracticeSeekSample g_seek_sample;
static uint64_t g_retry_generation;
static volatile uint64_t g_sk_events, g_sk_loop_events, g_sk_skips;
static volatile int g_sk_rc_getpos, g_sk_rc_setpos, g_sk_rc_clock;
static volatile uint32_t g_sk_pos_before, g_sk_pos_after;
static volatile int32_t g_sk_chart_before, g_sk_chart_after;
static volatile int g_last_chart_ms;
/* ds4f v33 route B: the PauseLayer stores the resume/retry target at +0x2a0
 * (constructor arg1). Cache that object + its vtable while paused; the loop
 * "重试" mode calls its vtable slot 2 (retry) while playing, then jumps to A
 * on the fresh chart. Slot 0 = resume (verified from the button lambdas). */
static volatile uint64_t g_retry_obj, g_retry_vptr, g_retry_calls, g_retry_fail;
static volatile int g_retry_valid;
static volatile uint64_t g_scene_gen;
static uint64_t g_song_bound_gen;
/* v35: the (chart - audio) offset is a property of the song; sample it while
 * actually playing and use it to derive the chart target at jump time, instead
 * of trusting a chart value captured while paused (t20 can keep running then). */
static volatile int32_t g_live_offset_ms;
static volatile int g_live_offset_valid;


unsigned pcp_ab_set(unsigned which)
{
    char line[96];
    int n;
    if (which>1 || !g_last_pos_ok) return 0;
    g_ab_pos[which]=(uint32_t)g_last_pos_ms;
    g_ab_chart[which]=(int32_t)g_last_chart_ms;
    g_ab_have[which]=1;
    n=snprintf(line,sizeof(line),"ab set=%c pos_ms=%d chart_ms=%d\n",
               which?'B':'A', g_last_pos_ms, g_last_chart_ms);
    if (n>0 && (size_t)n<sizeof(line)) padd(line,(size_t)n);
    return 1;
}

unsigned pcp_ab_have(unsigned which) { return which>1?0u:(unsigned)g_ab_have[which]; }
unsigned pcp_ab_get(unsigned which) { return which>1?0u:(unsigned)g_ab_pos[which]; }
unsigned pcp_ab_loop_get(void) { return (unsigned)g_ab_loop; }
void pcp_ab_loop_set(unsigned on) { g_ab_loop=on?1:0; }
unsigned pcp_ab_jumps(void) { return (unsigned)g_sk_events; }
unsigned pcp_seek_phase(void) { return (unsigned)g_seek.phase; }
unsigned pcp_seek_error(void) { return (unsigned)g_seek.error; }
void pcp_ab_jump(void) {
    uint64_t now=0;
    if (!pthread_main_np() || !pcp_rate_ui_state().visible || !g_ab_have[0]) return;
    if (ps_mach_us(mach_absolute_time(),g_tb_numer,g_tb_denom,&now))
        (void)psk_request(&g_seek,g_ab_pos[0],0,g_scene_gen,now);
}

static int safe_read(uint64_t addr, void *dst, uint64_t len); /* defined below */

static int retry_call(void)
{
    typedef void (*RetryFn)(void *, uint64_t);
    uint64_t vt=0, fn=0;
    if (!g_retry_obj || (g_retry_obj&7ull)) { g_retry_fail++; return -1; }
    if (!safe_read(g_retry_obj,&vt,8) || vt!=g_retry_vptr) {
        g_retry_valid=0; g_retry_fail++; return -1;
    }
    if (!safe_read(vt+0x10,&fn,8) || fn<0x100000000ull || fn>=0x200000000ull) {
        g_retry_fail++; return -1;
    }
    ((RetryFn)(uintptr_t)fn)((void *)(uintptr_t)g_retry_obj,0);
    g_retry_calls++;
    return 0;
}

unsigned pcp_bookmark_add(void)
{
    char line[96];
    int n;
    if (!g_last_pos_ok || g_bm_count>=PCP_BM_MAX) return (unsigned)g_bm_count;
    g_bm_pos[g_bm_count]=(uint32_t)g_last_pos_ms;
    g_bm_count++;
    n=snprintf(line,sizeof(line),"bm n=%llu pos_ms=%d\n",
               (unsigned long long)g_bm_count, g_last_pos_ms);
    if (n>0 && (size_t)n<sizeof(line)) padd(line,(size_t)n);
    return (unsigned)g_bm_count;
}
/* Main-thread drainer counters, not modified by hook callbacks. */
static uint64_t g_e1_count, g_e1_mismatch, g_e1_nonmain, g_e1_invalid;
static uint64_t g_main_base;

__attribute__((used, noinline))
uint64_t pcp_obs_entry(uint64_t a, uint64_t lr, uint64_t tag)
{
    PoqRecord r;
    uint32_t validbits = 0;
    int on_main=pthread_main_np();
    if (tag==8) {
        /* ds4f v27d/v27e: note scroll scalar bridge. a = chart object, lr = the
         * game's own value in float bits. The value is cached so the control
         * cycle can keep chart+0xf4 corrected; the store itself receives the
         * scaled value when a rate is active, otherwise the raw value. */
        uint32_t in=(uint32_t)lr, out=in;
        unsigned mode=atomic_load_explicit(&g_note_mode,memory_order_relaxed);
        uint32_t sb=atomic_load_explicit(&g_note_scale_bits,memory_order_relaxed);
        if (mode==PCP_NOTE_MODE_FIXED && sb) {
            float v,s;
            memcpy(&v,&in,4); memcpy(&s,&sb,4);
            v*=s; memcpy(&out,&v,4);
            g_scroll_baked_scale_bits=sb;
        } else {
            g_scroll_baked_scale_bits=0x3F800000u;  /* 1.0f */
        }
        g_scroll_calls++;
        g_scroll_chart=a;
        g_scroll_chart_valid=1;
        g_scroll_gen_at_hook=g_scroll_gen;
        /* On-site read: the hook runs inside the chart's own update, so the
         * pointer is live here (same pattern as the tag2 on-site fields). */
        if ((a & 7ull)==0 && (a-0x100000000ull)<0x100000000ull)
            g_scroll_vptr=*(volatile uint64_t *)(uintptr_t)a;
        g_scroll_raw_bits=in;
        g_scroll_out_bits=out;
        g_scroll_field_bits=out;
        return out;
    }
    if (tag==6) {
        ds4f_ui_probe_entry(a,lr);
        /* v33: cache the resume/retry target object (PauseLayer+0x2a0). */
        if (lr && (lr&7ull)==0) {
            uint64_t obj=*(volatile uint64_t *)(uintptr_t)(lr+0x2a0ull);
            if (obj && (obj&7ull)==0) {
                uint64_t vt=0;
                obj=obj;
                vt=*(volatile uint64_t *)(uintptr_t)obj;
                if (vt>0x100000000ull && vt<0x200000000ull) {
                    char pl[96];
                    int pn;
                    g_retry_obj=obj; g_retry_vptr=vt; g_retry_valid=1; g_retry_generation=g_scene_gen;
                    pn=snprintf(pl,sizeof(pl),"pc obj=%llx vt=%llx\n",
                                (unsigned long long)obj,(unsigned long long)vt);
                    if (pn>0 && (size_t)pn<sizeof(pl)) padd(pl,(size_t)pn);
                }
            }
        }
        return 0;
    }
    if (tag==9) { g_scroll_build_x1=lr; return 0; }
    if (tag==10) { g_scroll_build_x2=lr; return 0; }
    if (tag==3 && !on_main)
        atomic_store_explicit(&g_rate_badthread,1,memory_order_relaxed);
    if (tag==4 || tag==5) {
        if (!on_main) {atomic_store_explicit(&g_rate_badthread,1,memory_order_relaxed);return 0;}
        g_ui.epoch++;g_ui.visible=g_ui.ready=0;
        /* Scene destructor (tag4): the cached chart scalar belongs to the old
         * chart. tag5 also fires mid-song for BGM events, so it must not clear. */
        if (tag==4) {
            g_scroll_gen++; scroll_cache_clear();
            g_scene_gen++; g_retry_valid=0; g_live_offset_valid=0;
        }
        if (tag==4) g_rate_dtor_events++; else g_rate_play_events++;
        if ((g_rate.phase || g_rate.audio_owned) &&
            ((tag==4 && a==g_rate_owner.scene) || (tag==5 && a==g_rate_owner.pv)))
            prs_close(&g_rate,tag==4?2:3);
        return 0;
    }
    r.mach = mach_absolute_time();
    r.tag = tag;
    r.object = a;
    r.lr = tag == 3 ? 0 : lr; /* tag3 argument is native w9, NOT a caller LR */
    r.thread = (uint64_t)(uintptr_t)pthread_self();
    r.invocation = tag == 3 ? (uint32_t)lr : 0; /* tag3: native input bits */
    r.t20 = 0;
    r.t28 = 0;
    r.state = 0;
    {
        uint64_t tl = 0;
        if (tag == 1 || tag == 3) {
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
    if (on_main) {
        validbits |= OBS_VALID_MAIN;
    }
    r.valid = validbits;
    (void)poq_push(&g_obs_q, &r);
    if (on_main && tag==1 && (r.valid & OBS_VALID_READS) &&
        (r.state & 255u)!=1 && (g_ui.visible || g_ui.pending)) {
        g_ui.epoch++;g_ui.visible=g_ui.ready=0;
        if (g_ui.pending) {g_ui.pending=0;g_ui.result=-1;}
    }
    if (tag==1 && on_main && g_rate.phase && a==g_rate_owner.timeline &&
        ((r.state >> 8) & 255u)!=1) prs_close(&g_rate,7);
    if (tag==3 && on_main) {
        uint64_t now_us=0;
        g_rate_last_tl=a;g_rate_last_mach=r.mach;
        g_rate_last_native=(int32_t)(uint32_t)lr;g_rate_last_state=r.state;
        if (g_rate.phase && a==g_rate_owner.timeline &&
            ps_mach_us(r.mach,g_tb_numer,g_tb_denom,&now_us)) {
            int paused=(r.state & 255u)==1;
            /* ds4f v27c: the former t30/t34/t38 "must be zero" fallback treated a
             * per-song sync/boundary constant as an error. On songs with t30 != 0 it
             * fired every tick (log (20): errors grew ~120/s, any applied rate was
             * reverted to 1x within a second). The clock model already faults on real
             * native-time jumps and the drainer closes sessions on identity change, so
             * no extra check is needed here; the values stay visible in the p-line. */
            return (uint32_t)prs_tick(&g_rate,now_us,(int32_t)(uint32_t)lr,paused);
        }
    }
    return tag==3?(uint32_t)lr:0;
}


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
    unsigned int note_mode = 0;
    uint64_t scroll_delta = 0;
    uint64_t scroll_writes = 0, scroll_fail = 0;
    uint64_t scroll_relayouts = 0, scroll_relayout_fail = 0;
    float sc_raw = 0.0f, sc_out = 0.0f;
    float sc_field = 0.0f;
    float sc_desired = 0.0f, sc_baked = 1.0f;
    unsigned int pitch_mode = 0;
    int pitch_attached = 0;
    int pitch_comp = 0;
    float pitch_ratio = 1.0f, pitch_readback = 0.0f;
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
        g_seek_sample=(PracticeSeekSample){0};
        if (cs.state == CH_READY) {
            ct_probe(&tl, &ctCd, &ctA, &t20, &t24, &t28, &f2c, &f2d, &f2e,
                     &sf, &s470, &s474, &t30, &sess_addr, &scene_addr, &xf,
                     (uint64_t)task->q_seq, via, sizeof(via), &dg, &te);
        }
        /* ds4f v26: per-song scope, prep-phase arming, auto-apply at the first live
         * sample. Requests carry only a percent and lifecycle epoch, never a pointer. */
        {
            uint64_t marker=0,now_us=0,last_us=0;
            uint32_t dtor_word=0,play_word=0;
            int main_ok,scene_ok,paused,time_ok,fresh_paused,fresh_playing,patched;
            int prep,live,playing;
            main_ok = pthread_main_np() &&
                !atomic_load_explicit(&g_rate_badthread,memory_order_relaxed);
            scene_ok = ctCd && ctA && tl!=0 && scene_addr!=0 && cs.chan0!=0 && pok;
            paused = (f2c==1 && f2d==1 && sf==1 && s470==1);
            time_ok = ps_mach_us(mach_absolute_time(),g_tb_numer,g_tb_denom,&now_us) &&
                ps_mach_us(g_rate_last_mach,g_tb_numer,g_tb_denom,&last_us) &&
                now_us>=last_us && now_us-last_us<100000;
            /* State bytes are little-endian: low byte = f2c (+0x2c), next = f2d (+0x2d).
             * Playing is f2c=0,f2d=1 -> 0x0100; paused is 1,1 -> 0x0101. Log (18)
             * showed the previous 0x0001 expectation never matched on device. */
            fresh_paused = g_rate_last_tl==tl && (g_rate_last_state & 0xffu)==1u &&
                ((g_rate_last_state >> 8) & 0xffu)==1u && time_ok;
            fresh_playing = g_rate_last_tl==tl && (g_rate_last_state & 0xffu)==0u &&
                ((g_rate_last_state >> 8) & 0xffu)==1u && time_ok;
            patched = safe_read(g_base+OFF_SETPITCH,&marker,8) && marker==0x6d1223e9d10583ffULL &&
                safe_read(g_base+0x9237fc,&dtor_word,4) && dtor_word==0x17db8461u &&
                safe_read(g_base+0x8e3900,&play_word,4) && play_word==0x17dc8480u;
            prep = main_ok && scene_ok && paused && f2e==0 && fresh_paused && patched;
            /* ds4f v27c: t30/t34/t38 are per-song boundary/sync metadata, not error
             * or readiness signals. Log (20) shows a legitimate song with t30=-1371,
             * t38=1371 for the whole session; the old "all zero" live gate excluded
             * it (ready=0 while paused) and the tag3 path below also reverted every
             * apply. Readiness now relies on freshness, identity and position only. */
            live = main_ok && scene_ok && paused && f2e==1 &&
                (int64_t)t20-t28>1000 && fresh_paused && patched;
            playing = main_ok && scene_ok && !paused && f2c==0 && f2e==1 &&
                (int64_t)t20-t28>0 && fresh_playing && patched;

            if (scene_ok && (scene_addr!=g_song_scene || g_song_bound_gen!=g_scene_gen)) {
                g_song_bound_gen=g_scene_gen;
                if (g_rate.phase || g_rate.audio_owned) prs_close(&g_rate,2);
                memset(&g_rate,0,sizeof(g_rate));
                g_live_offset_valid=0;
                g_song_scene=scene_addr;g_song_timeline=tl;
                g_song_started=0;g_song_attempts=0;g_rate_stable=0;
                g_setting_percent=pcp_rate_ui_stored_percent();
                g_ui.epoch++;g_ui.pending=0;g_ui.result=0;
            } else if (!scene_ok && g_song_scene) {
                if (g_rate.phase || g_rate.audio_owned) prs_close(&g_rate,2);
                g_live_offset_valid=0;
                g_song_scene=0;g_song_timeline=0;g_song_started=0;
                g_song_attempts=0;g_rate_stable=0;g_ui.epoch++;
            } else if (scene_ok && g_song_timeline && tl!=g_song_timeline) {
                if (g_rate.phase || g_rate.audio_owned) prs_close(&g_rate,6);
                g_song_timeline=tl;g_song_started=0;g_song_attempts=0;g_rate_stable=0;
            }
            if ((g_rate.phase || g_rate.audio_owned) && scene_ok && !rate_current(&g_rate_owner)) {
                prs_close(&g_rate,6);
                g_song_started=0;g_song_attempts=0;g_rate_stable=0;
            }
            g_ui_seen_mach=mach_absolute_time();
            g_ui.visible = ctCd && ctA && paused;
            /* v38: sample the actual consumer, excluding paused/preroll samples. */
            g_seek_sample=(PracticeSeekSample){.now_us=now_us,.generation=g_scene_gen,
                .valid=main_ok && scene_ok && patched && time_ok,
                .playing=playing && pos>0,.pos=pos>=0?(uint32_t)pos:0,
                .consumer=(int64_t)t20-t28,.percent=g_setting_percent};
            if (playing && pos>0) {
                int64_t off=(int64_t)t20-t28-pos;
                if (off>-20000 && off<20000) {
                    g_live_offset_ms=(int32_t)off;g_live_offset_valid=1;
                }
            }
            g_ui.prep = prep?1:0;
            g_ui.ready = 0;
            if (prep || live) {
                if (g_rate_candidate_tl!=tl || g_rate_candidate_handle!=cs.chan0) g_rate_stable=0;
                g_rate_candidate_tl=tl;g_rate_candidate_handle=cs.chan0;
                if (g_rate_stable<2) ++g_rate_stable;
                g_ui.ready = g_rate_stable>=2;
            } else {
                g_rate_stable=0;
            }
            if (g_ui.pending) {
                g_ui.pending=0;g_ui.result=-1;
                if (g_ui.ready && g_ui_request_epoch==g_ui.epoch && (prep || live) &&
                    g_song_scene==scene_addr &&
                    (!g_rate.phase || (g_rate_owner.timeline==tl && g_rate_owner.scene==scene_addr &&
                     g_rate_owner.handle==cs.chan0 && rate_current(&g_rate_owner)))) {
                    PracticePitchOps ops={&g_rate_owner,rate_get,rate_set};
                    if (!g_rate.phase && !g_rate.audio_owned)
                        g_rate_owner=(rate_owner_t){cs.am,cs.pv,cs.chan0,scene_addr,tl};
                    g_rate_requested=g_ui_request_percent;
                    g_setting_percent=g_ui_request_percent;
                    g_ui.result=prs_configure(&g_rate,now_us,g_rate_last_native,g_rate_requested,ops,rate_current)?1:-2;
                }
            }
            if (playing && !g_song_started && g_song_attempts<3 &&
                g_setting_percent>=PCP_RATE_MIN_PERCENT && g_setting_percent<=PCP_RATE_MAX_PERCENT) {
                if (g_setting_percent==100 && !g_rate.phase) {
                    if (g_ab_have[0] || g_ab_have[1] || g_ab_loop) {
                        /* v35: A/B needs a clock session even at exactly 1x so a
                         * jump can rebase the chart clock; create one on demand. */
                        int applied=0;
                        PracticePitchOps ops={&g_rate_owner,rate_get,rate_set};
                        g_song_attempts++;
                        g_rate_owner=(rate_owner_t){cs.am,cs.pv,cs.chan0,scene_addr,tl};
                        g_rate_requested=100;
                        applied=prs_configure(&g_rate,now_us,g_rate_last_native,100,ops,rate_current);
                        g_ui.result=applied?1:-2;
                        if (applied || g_song_attempts>=3) g_song_started=1;
                    } else {
                        g_song_started=1;   /* nothing to apply */
                    }
                } else {
                    int applied=0;
                    g_song_attempts++;
                    if (g_rate.phase && g_rate_owner.timeline==tl &&
                        g_rate_owner.handle==cs.chan0) {
                        g_rate_requested=g_setting_percent;
                        applied=prs_live_begin(&g_rate,now_us,g_rate_last_native,g_setting_percent);
                    } else {
                        PracticePitchOps ops={&g_rate_owner,rate_get,rate_set};
                        if (g_rate.phase || g_rate.audio_owned) prs_close(&g_rate,9);
                        g_rate_owner=(rate_owner_t){cs.am,cs.pv,cs.chan0,scene_addr,tl};
                        g_rate_requested=g_setting_percent;
                        applied=prs_configure(&g_rate,now_us,g_rate_last_native,g_setting_percent,ops,rate_current);
                    }
                    g_ui.result=applied?1:-2;
                    if (applied || g_song_attempts>=3) g_song_started=1;
                }
            }
            /* v36: when A/B is armed, keep a 1x clock session alive so jumps are
             * never skipped for lack of a session (created while natively paused). */
            if (!g_rate.phase && paused && scene_ok && cs.chan0 && pok &&
                (g_ab_have[0] || g_ab_have[1] || g_ab_loop)) {
                PracticePitchOps ops={&g_rate_owner,rate_get,rate_set};
                g_rate_owner=(rate_owner_t){cs.am,cs.pv,cs.chan0,scene_addr,tl};
                g_rate_requested=100;
                (void)prs_configure(&g_rate,now_us,g_rate_last_native,100,ops,rate_current);
                g_ui.result=1;
            }
        }
        /* ds4f v27d/v27e/v27f: publish the note scroll multiplier, keep
         * chart+0xf4 corrected, and - when the chart's baked note geometry
         * still uses the old multiplier - repeat the game's own geometry
         * rebuild (0x910CE0, captured at the call site) while paused.
         * phase==1 means a non-1x rate is configured for this song; with mode
         * 同步 (default) or an inactive session the multiplier stays 1.0. */
        {
            unsigned mode = pcp_note_mode_get();
            float scale = 1.0f;
            if (mode==PCP_NOTE_MODE_FIXED && g_rate.phase==1 && g_rate.clock.percent)
                scale = 100.0f/(float)g_rate.clock.percent;
            uint32_t bits;
            int field_ok = 0;
            memcpy(&bits,&scale,4);
            atomic_store_explicit(&g_note_mode,mode,memory_order_relaxed);
            atomic_store_explicit(&g_note_scale_bits,bits,memory_order_relaxed);
            /* v27e: the game stores chart+0xf4 only when the chart is rebuilt,
             * so a rate or mode change has to be written by us. The field is
             * re-read through the checked interface and only overwritten while
             * it still holds the game's raw value or our own previous value.
             * Only runs while a rate session is active for the current chart;
             * the tag4 scene destructor clears the cache (generation check). */
            if (g_scroll_chart_valid && g_scroll_chart && g_rate.phase &&
                g_scroll_gen_at_hook==g_scroll_gen) {
                uint32_t raw_bits = g_scroll_raw_bits;
                float raw, want;
                uint32_t want_bits, field = 0;
                uint64_t vptr = 0;
                memcpy(&raw,&raw_bits,4);
                want = raw*scale;
                memcpy(&want_bits,&want,4);
                if (g_scroll_vptr && safe_read((uint64_t)g_scroll_chart,&vptr,8) &&
                    vptr==g_scroll_vptr &&
                    raw>0.01f && raw<100.0f && scale>=0.4f && scale<=2.5f &&
                    safe_read((uint64_t)g_scroll_chart+0xf4ull,&field,4)) {
                    if (field != want_bits) {
                        if (field == raw_bits || field == g_scroll_field_bits) {
                            *(volatile uint32_t *)(uintptr_t)((uint64_t)g_scroll_chart+0xf4ull)=want_bits;
                            g_scroll_writes++;
                            field=want_bits;
                        } else {
                            /* the game rebuilt the chart: adopt the new raw value */
                            g_scroll_raw_bits=field;
                        }
                    }
                    g_scroll_field_bits=field;
                    field_ok = (field==want_bits);
                } else {
                    g_scroll_chart_valid=0;
                    g_scroll_chart=0;
                    g_scroll_write_fail++;
                }
            }
            /* v27g: the v27f relayout replay crashed (log 24 + two .ips): the
             * captured call arguments are not valid mid-song, and replaying
             * 0x910CE0 with them produced a bad virtual call
             * (KERN_PROTECTION_FAILURE at 0x2000000003). The replay is removed;
             * the captured context stays as diagnostics only. The note mode is
             * baked at the next chart build (retry / re-enter), as in v27e. */
            (void)field_ok;
            (void)bits;
        }
        /* ds4f v28 stage 1: one-shot pitch DSP lifecycle probe while paused. */
        if (!g_pitch_probe_done && g_ui.visible && cs.chan0) {
            int hit=pitch_probe_run((void *)(uintptr_t)cs.chan0);
            char pl[240];
            int pn=snprintf(pl,sizeof(pl),
                "pd seq=%llu hit=%d sys=%llx dsp=%llx type=%d name=%s fft=%.0f lat_ms=%.2f rc=%d/%d/%d/%d rel=%d\n",
                (unsigned long long)task->q_seq, hit,
                (unsigned long long)g_pitch_sys, (unsigned long long)g_pitch_dsp,
                g_pitch_type, g_pitch_name, (double)g_pitch_fft, (double)g_pitch_lat_ms,
                g_pitch_rc_sys, g_pitch_rc_create, g_pitch_rc_info, g_pitch_rc_get, g_pitch_rc_rel);
            if (pn>0 && (size_t)pn<sizeof(pl)) padd(pl,(size_t)pn);
        }
        /* ds4f v29: keep-pitch attach/restore (attach only while paused). */
        {
            unsigned pmode=pcp_pitch_mode_get();
            int keep=(pmode==PCP_PITCH_KEEP);
            float ratio=1.0f;
            if (keep && g_rate.phase==1 && g_rate.clock.percent)
                ratio=100.0f/(float)g_rate.clock.percent;
            if (keep && g_rate.phase==1 && cs.chan0 && (g_pd_dsp || g_ui.visible)) {
                float before=g_pd_ratio;
                uint64_t att0=g_pd_attach_events;
                if (g_pd_dsp && g_pd_attached && (int)pcp_pitch_comp_get()!=g_pd_last_comp)
                    pd_restore();   /* compensation changed: re-attach cleanly */
                if (pd_apply(ratio,(void *)(uintptr_t)cs.chan0) &&
                    (ratio!=before || g_pd_attach_events!=att0)) {
                    char al[260];
                    int an=snprintf(al,sizeof(al),
                        "pa seq=%llu act=set chan=%llx dsp=%llx ratio=%.3f rb=%.3f comp=%d bump=%u pos=%u/%u rcp=%d/%d rc=%d/%d/%d/%d/%d/%d att=%llu det=%llu\n",
                        (unsigned long long)task->q_seq,(unsigned long long)cs.chan0,
                        (unsigned long long)(uintptr_t)g_pd_dsp,(double)ratio,(double)g_pd_readback,
                        g_pd_comp_ms,(unsigned)g_pd_bump,(unsigned)g_pd_pos_before,(unsigned)g_pd_pos_after,
                        g_pd_rc_getpos,g_pd_rc_setpos,
                        g_pd_rc_create,g_pd_rc_add,g_pd_rc_set,g_pd_rc_get,g_pd_rc_rm,g_pd_rc_rel,
                        (unsigned long long)g_pd_attach_events,(unsigned long long)g_pd_detach_events);
                    if (an>0 && (size_t)an<sizeof(al)) padd(al,(size_t)an);
                }
            } else if (g_pd_dsp) {
                pd_restore();
                {
                    char al[160];
                    int an=snprintf(al,sizeof(al),
                        "pa seq=%llu act=off rc=%d/%d/%d/%d/%d/%d att=%llu det=%llu\n",
                        (unsigned long long)task->q_seq,
                        g_pd_rc_create,g_pd_rc_add,g_pd_rc_set,g_pd_rc_get,g_pd_rc_rm,g_pd_rc_rel,
                        (unsigned long long)g_pd_attach_events,(unsigned long long)g_pd_detach_events);
                    if (an>0 && (size_t)an<sizeof(al)) padd(al,(size_t)an);
                }
            }
        }
        /* v38: one restart/locate/verify transaction for manual and loop jumps.
         * All calls execute on this main-thread sampling callback. No seek in pause. */
        {
            PracticeSeekSample x=g_seek_sample;
            char line[320]; int n=0;
            x.valid=x.valid && ctA && ctCd && pok && g_rate.phase &&
                rate_current(&g_rate_owner) && g_rate_owner.timeline==tl &&
                g_rate_owner.scene==scene_addr && g_rate_owner.handle==cs.chan0;
            x.can_restart=g_retry_valid && g_retry_generation==g_scene_gen;
            x.percent=g_rate.clock.percent;
            if (!psk_busy(&g_seek) && g_ab_loop && g_ab_have[0] && g_ab_have[1] &&
                g_ab_pos[1]>g_ab_pos[0] && x.valid && x.playing && x.pos>=g_ab_pos[1])
                (void)psk_request(&g_seek,g_ab_pos[0],1,g_scene_gen,x.now_us);
            int old_phase=g_seek.phase;
            int action=psk_poll(&g_seek,x);
            if (action==PSK_RESTART) {
                int rc=retry_call();
                if (rc) psk_fail(&g_seek,10);
                n=snprintf(line,sizeof(line),"seek id=%llu act=restart target=%u gen=%llu rc=%d auto=%d\n",
                    (unsigned long long)g_seek.id,g_seek.target,(unsigned long long)g_scene_gen,rc,g_seek.automatic);
            } else if (action==PSK_LOCATE) {
                typedef int32_t (*PosFn)(void *,uint32_t,uint32_t);
                typedef int32_t (*ReadFn)(void *,uint32_t *,uint32_t);
                int32_t anchor=0; uint32_t actual=0;
                int rc=-1,rb=-1;
                int64_t target_consumer=(int64_t)g_seek.target+g_seek.offset;
                if (x.playing && safe_read(tl+0x28,&anchor,4) &&
                    target_consumer+anchor>=INT32_MIN && target_consumer+anchor<=INT32_MAX) {
                    rc=((PosFn)(uintptr_t)(g_base+OFF_CC_SETPOS))((void *)(uintptr_t)cs.chan0,g_seek.target,1);
                    if (!rc) {
                        g_rate.clock.bias_us=(target_consumer+anchor-(int64_t)g_rate_last_native)*1000;
                        g_rate.clock.fraction=0;
                        rb=((ReadFn)(uintptr_t)(g_base+OFF_CC_GETPOS))((void *)(uintptr_t)cs.chan0,&actual,1);
                    }
                }
                if (rc || rb || llabs((int64_t)actual-g_seek.target)>150) psk_fail(&g_seek,11);
                n=snprintf(line,sizeof(line),"seek id=%llu act=locate target=%u actual=%u consumer_target=%lld anchor=%d offset=%d rc=%d/%d gen=%llu\n",
                    (unsigned long long)g_seek.id,g_seek.target,actual,(long long)target_consumer,
                    anchor,g_seek.offset,rc,rb,(unsigned long long)g_scene_gen);
            } else if (action==PSK_SUCCESS) {
                if (g_seek.automatic) g_sk_loop_events++; else g_sk_events++;
            }
            if (n>0 && (size_t)n<sizeof(line)) padd(line,(size_t)n);
            if (psk_busy(&g_seek) || old_phase!=g_seek.phase) {
                n=snprintf(line,sizeof(line),"seek id=%llu phase=%d error=%d target=%u actual=%u consumer=%lld gen=%llu playing=%d valid=%d stable=%d verified=%d\n",
                    (unsigned long long)g_seek.id,g_seek.phase,g_seek.error,g_seek.target,x.pos,
                    (long long)x.consumer,(unsigned long long)g_scene_gen,x.playing,x.valid,g_seek.stable,g_seek.verified);
                if (n>0 && (size_t)n<sizeof(line)) padd(line,(size_t)n);
            }
            if (g_seek.phase==PSK_FAILED && old_phase!=PSK_FAILED) {
                g_ab_loop=0;g_sk_skips++;
                /* A partial locate must not keep running with mismatched clocks.
                 * Reset through the native retry path once, with looping disabled. */
                if ((old_phase==PSK_VERIFY || action==PSK_LOCATE) &&
                    g_retry_valid && g_retry_generation==g_scene_gen) {
                    int rc=retry_call();
                    n=snprintf(line,sizeof(line),"seek id=%llu act=recover-retry rc=%d\n",(unsigned long long)g_seek.id,rc);
                    if (n>0 && (size_t)n<sizeof(line)) padd(line,(size_t)n);
                }
            }
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
            for (j = 0; j < 1024; j++) {
                if (poq_pop(&g_obs_q, &rec) != POQ_OK) {
                    break;
                }
                uc++;
                if (rec.tag == 3) {
                    g_e1_count++;
                    if (!(rec.valid & OBS_VALID_MAIN)) g_e1_nonmain++;
                    if (!(rec.valid & OBS_VALID_READS)) g_e1_invalid++;
                    else if ((uint32_t)rec.t20 != (uint32_t)rec.invocation) g_e1_mismatch++;
                }
                if (!g_rate.ever) ps_observe(&g_shadow, &rec, g_tb_numer, g_tb_denom);
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
                    uint64_t gap_us = 0;
                    (void)ps_mach_us(rec.mach - prev_mach, g_tb_numer, g_tb_denom, &gap_us);
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
    /* ds4f v27d: tag8 note-scroll bridge counters for this cycle. */
    note_mode = atomic_load_explicit(&g_note_mode, memory_order_relaxed);
    pitch_mode = pcp_pitch_mode_get();
    pitch_attached = g_pd_attached;
    pitch_comp = g_pd_comp_ms;
    pitch_ratio = g_pd_ratio;
    pitch_readback = g_pd_readback;
    g_last_pos_ms = pos;
    g_last_pos_ok = (ok && pos>=0);
    g_last_chart_ms = t20;
    {
        uint64_t calls = g_scroll_calls;
        scroll_delta = calls - g_scroll_prev_calls;
        g_scroll_prev_calls = calls;
        uint32_t rb = g_scroll_raw_bits, ob = g_scroll_out_bits;
        uint32_t fb = g_scroll_field_bits;
        memcpy(&sc_raw, &rb, 4);
        memcpy(&sc_out, &ob, 4);
        memcpy(&sc_field, &fb, 4);
        scroll_writes = g_scroll_writes;
        scroll_fail = g_scroll_write_fail;
        scroll_relayouts = g_scroll_relayouts;
        scroll_relayout_fail = g_scroll_relayout_fail;
        {
            uint32_t sb = atomic_load_explicit(&g_note_scale_bits,memory_order_relaxed);
            uint32_t bb = g_scroll_baked_scale_bits;
            float cur_scale = 1.0f, baked_scale = 0.0f;
            if (sb) memcpy(&cur_scale,&sb,4);
            if (bb) memcpy(&baked_scale,&bb,4);
            sc_desired = sc_raw*cur_scale;
            sc_baked = baked_scale;
        }
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
    if (n > 0 && (size_t)n < sizeof(line) - 96) {
        n += snprintf(line + n, sizeof(line) - (size_t)n,
                      " scm=%u scc=%llu scr=%.3f sco=%.3f scd=%.3f scf=%.3f scb=%.3f scw=%llu scx=%llu srl=%llu srf=%llu"
                      " ptm=%u pta=%d ptr=%.3f ptb=%.3f ptc=%d",
                      note_mode, (unsigned long long)scroll_delta,
                      (double)sc_raw, (double)sc_out, (double)sc_desired,
                      (double)sc_field, (double)sc_baked,
                      (unsigned long long)scroll_writes,
                      (unsigned long long)scroll_fail,
                      (unsigned long long)scroll_relayouts,
                      (unsigned long long)scroll_relayout_fail,
                      pitch_mode, pitch_attached, (double)pitch_ratio,
                      (double)pitch_readback, pitch_comp);
    }
    if (n > 0 && (size_t)n < sizeof(line) - 64) {
        n += snprintf(line + n, sizeof(line) - (size_t)n,
                      " ab=%d/%d/%d skc=%llu rtc=%llu rtf=%llu",
                      g_ab_have[0], g_ab_have[1], g_ab_loop,
                      (unsigned long long)(g_sk_events+g_sk_loop_events),
                      (unsigned long long)g_retry_calls,
                      (unsigned long long)g_retry_fail);
    }
    if (n > 0 && (size_t)n < sizeof(line) - 2) {
        line[n++] = '\n';
        line[n] = '\0';
        padd(line, (size_t)n);
    }
    n = snprintf(line, sizeof(line),
                 "sh seq=%llu segments=%llu compared=%llu skipped=%llu faults=%llu transitions=%llu"
                 " over5=%llu over50=%llu max_us=%llu last_us=%lld bound=%d paused=%d\n",
                 (unsigned long long)task->q_seq,
                 (unsigned long long)g_shadow.segments, (unsigned long long)g_shadow.compared,
                 (unsigned long long)g_shadow.skipped, (unsigned long long)g_shadow.faults,
                 (unsigned long long)g_shadow.transitions, (unsigned long long)g_shadow.over5ms,
                 (unsigned long long)g_shadow.over50ms, (unsigned long long)g_shadow.max_abs_us,
                 (long long)g_shadow.last_error_us, g_shadow.bound, g_shadow.paused);
    if (n > 0 && (size_t)n < sizeof(line)) padd(line, (size_t)n);
    n = snprintf(line, sizeof(line),
                 "e1 seq=%llu calls=%llu input_mismatch=%llu nonmain=%llu invalid=%llu rate=%u\n",
                 (unsigned long long)task->q_seq, (unsigned long long)g_e1_count,
                 (unsigned long long)g_e1_mismatch, (unsigned long long)g_e1_nonmain,
                 (unsigned long long)g_e1_invalid, g_rate.phase?g_rate.clock.percent:100);
    if (n > 0 && (size_t)n < sizeof(line)) padd(line, (size_t)n);
    if (!ok) {
        pflush();   /* keep skip events durable */
    }
    n = snprintf(line,sizeof(line),
        "rt seq=%llu wanted=%u ever=%d phase=%d rate=%u owned=%d errors=%d reason=%d"
        " ticks=%llu restores=%llu bias_us=%lld baseline=%.4f applied=%.4f readback=%.4f restored=%.4f"
        " rc=%d/%d/%d/%d life=%llu/%llu badthread=%d shadow_active=%d setting=%u started=%d attempts=%d\n",
        (unsigned long long)task->q_seq,g_rate_requested,g_rate.ever,g_rate.phase,
        g_rate.phase?g_rate.clock.percent:100,g_rate.audio_owned,g_rate.errors,g_rate.close_reason,
        (unsigned long long)g_rate.ticks,(unsigned long long)g_rate.restores,(long long)g_rate.clock.bias_us,
        (double)g_rate.baseline,(double)g_rate.applied,(double)g_rate.readback,(double)g_rate.restored,
        g_rate.apply_rc,g_rate.read_rc,g_rate.restore_rc,g_rate.restore_read_rc,
        (unsigned long long)g_rate_dtor_events,(unsigned long long)g_rate_play_events,
        atomic_load_explicit(&g_rate_badthread,memory_order_relaxed),!g_rate.ever,
        g_setting_percent,g_song_started,g_song_attempts);
    if(n>0 && (size_t)n<sizeof(line)) padd(line,(size_t)n);
    n=snprintf(line,sizeof(line),"ui seq=%llu epoch=%llu visible=%d ready=%d pending=%d result=%d prep=%d requested=%u\n",
        (unsigned long long)task->q_seq,(unsigned long long)g_ui.epoch,g_ui.visible,g_ui.ready,
        g_ui.pending,g_ui.result,g_ui.prep,g_rate_requested);
    if(n>0 && (size_t)n<sizeof(line)) padd(line,(size_t)n);
    if(g_uip_seq!=g_uip_logged) {
        g_uip_logged=g_uip_seq;
        n=snprintf(line,sizeof(line),
            "uip seq=%llu node=%llx layer=%llx child=%llx slot=%d inst=%d prac=%llx fake=%llx\n",
            (unsigned long long)g_uip_logged,(unsigned long long)g_uip_node,
            (unsigned long long)g_uip_layer,(unsigned long long)g_uip_child,
            g_uip_slot_ok?1:0,g_uip_install,
            (unsigned long long)g_uip_practice,(unsigned long long)g_uip_fake);
        if(n>0 && (size_t)n<sizeof(line)) padd(line,(size_t)n);
    }
    if(g_rate.ever || g_uip_seq) pflush();
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
    practice_rate_ui_start();
    g_setting_percent=pcp_rate_ui_stored_percent();
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
        int shadow_ok = ps_init(&g_shadow);
        if (main_base != 0 && qok && shadow_ok && g_tb_numer && g_tb_denom) {
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
