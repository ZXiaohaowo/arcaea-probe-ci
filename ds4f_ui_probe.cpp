// ds4f v27a native practice entry (route A).
//
// Called from the PauseLayer init trampoline (tag=6) with the freshly loaded
// PauseOverlay node and the PauseLayer this pointer.
//   1. read-only sanity check: node->getChildByName("resumeButton-chinaonlylocalize")
//   2. native entry install: load a second PauseOverlay tree, take its retry button,
//      relabel it "练习", move the unused children off-screen, reflow the three
//      native buttons into a four-slot row, attach a touch listener that opens the
//      rate panel, and add the second tree under the real overlay.
// All engine access uses slots/addresses confirmed by static disassembly:
//   setPosition = vtable+0x98, addChild = vtable+0x1E8, getChildByName = vtable+0x210
//   loader 0xF35924, Label::setString 0xE4C02C, addTouchEventListener 0xE10728
#include <cstdint>
#include <functional>
#include <string>
#include <mach-o/dyld.h>

extern "C" {
volatile uint64_t g_uip_node, g_uip_layer, g_uip_child, g_uip_seq;
volatile int g_uip_slot_ok;
volatile int g_uip_install;
volatile uint64_t g_uip_practice, g_uip_fake;
void pcp_rate_ui_open(void);
}

namespace {

constexpr uint64_t kLoaderRva = 0xF35924;
constexpr uint64_t kSetStringRva = 0xE4C02C;
constexpr uint64_t kAddTouchRva = 0xE10728;
constexpr uint64_t kSetPosSlot = 0x98;
constexpr uint64_t kAddChildSlot = 0x1E8;
constexpr uint64_t kGetChildSlot = 0x210;

using GetChildFn = void *(*)(void *, const std::string *);
using SetPosFn = void (*)(void *, const float *);
using AddChildFn = void (*)(void *, void *);
using AddTouchFn = void (*)(void *, const std::function<void(void *, int)> *);
using SetStringFn = void (*)(void *, const std::string *);
using LoadFn = void *(*)(const std::string *);

void *vslot(void *obj, uint64_t offset)
{
    if (!obj) return nullptr;
    void **vt = *(void ***)obj;
    if (!vt || ((uintptr_t)vt & 7ull)) return nullptr;
    uint64_t fn = (uint64_t)(uintptr_t)vt[offset / 8];
    if (fn < 0x100000000ull || fn >= 0x200000000ull) return nullptr;
    return (void *)(uintptr_t)fn;
}

void *getChild(void *node, const char *name)
{
    auto fn = (GetChildFn)vslot(node, kGetChildSlot);
    if (!fn) return nullptr;
    std::string s(name);
    return fn(node, &s);
}

void setPos(void *node, float x, float y)
{
    auto fn = (SetPosFn)vslot(node, kSetPosSlot);
    if (!fn) return;
    float p[2] = {x, y};
    fn(node, p);
}

}  // namespace

extern "C" void ds4f_ui_probe_entry(uint64_t node, uint64_t layer)
{
    g_uip_node = node;
    g_uip_layer = layer;
    g_uip_child = 0;
    g_uip_slot_ok = 0;
    g_uip_install = 0;
    g_uip_practice = 0;
    g_uip_fake = 0;
    g_uip_seq++;
    if (!node || (node & 7ull)) return;

    uint64_t vt = *(volatile uint64_t *)(uintptr_t)node;
    if (!vslot((void *)(uintptr_t)node, kGetChildSlot)) {
        (void)vt;
        return;
    }
    g_uip_slot_ok = 1;

    /* read-only check kept for continuity with the v26 probe */
    g_uip_child = (uint64_t)(uintptr_t)getChild(
        (void *)(uintptr_t)node, "resumeButton-chinaonlylocalize");

    void *overlay = (void *)(uintptr_t)node;
    uintptr_t base = (uintptr_t)_dyld_get_image_header(0);
    if (!base) { g_uip_install = -10; return; }

    std::string path("layouts/ingame/PauseOverlay.csb");
    void *fake = ((LoadFn)(base + kLoaderRva))(&path);
    if (!fake) { g_uip_install = -11; return; }
    if (fake == overlay) { g_uip_install = -12; return; }
    g_uip_fake = (uint64_t)(uintptr_t)fake;

    void *practice = getChild(fake, "retryButton-chinaonlylocalize");
    if (!practice) { g_uip_install = -13; return; }
    void *label = getChild(practice, "retry_text-chinaonlylocalize");
    if (!label) { g_uip_install = -14; return; }

    std::string text(u8"\u7ec3\u4e60");  /* 练习 */
    ((SetStringFn)(base + kSetStringRva))(label, &text);

    /* keep only the practice button visible in the second tree */
    static const char *kHide[] = {
        "darken", "bg", "pauseText", "multiplayer_disconnected_text",
        "multiplayer_disconnected_icon", "multiplayer_disconnecting_text",
        "multiplayer_number_text", "resumeButton-chinaonlylocalize",
        "quitButton-chinaonlylocalize",
    };
    for (const char *name : kHide) {
        void *n = getChild(fake, name);
        if (n) setPos(n, 0.0f, -30000.0f);
    }
    setPos(practice, 768.0f, 292.0f);

    /* four-slot native row: 256 / 512 / 768 (practice) / 1024 */
    if (void *r = getChild(overlay, "resumeButton-chinaonlylocalize")) setPos(r, 256.0f, 292.0f);
    if (void *r = getChild(overlay, "retryButton-chinaonlylocalize")) setPos(r, 512.0f, 292.0f);
    if (void *r = getChild(overlay, "quitButton-chinaonlylocalize")) setPos(r, 1024.0f, 292.0f);

    std::function<void(void *, int)> cb = [](void *, int type) {
        if (type == 2) pcp_rate_ui_open();
    };
    ((AddTouchFn)(base + kAddTouchRva))(practice, &cb);

    auto addChild = (AddChildFn)vslot(overlay, kAddChildSlot);
    if (!addChild) { g_uip_install = -15; return; }
    addChild(overlay, fake);

    g_uip_practice = (uint64_t)(uintptr_t)practice;
    g_uip_install = 1;
}
