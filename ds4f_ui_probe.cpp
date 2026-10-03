// ds4f v26 read-only UI probe.
//
// Called from the PauseLayer init trampoline (tag=6) with the freshly loaded
// PauseOverlay node and the PauseLayer this pointer. It performs ONE virtual
// call - node->getChildByName("resumeButton-chinaonlylocalize") - and stores the
// result for the main-thread logger. No game state is written.
#include <cstdint>
#include <string>

extern "C" {
volatile uint64_t g_uip_node, g_uip_layer, g_uip_child, g_uip_seq;
volatile int g_uip_slot_ok;
}

// PauseOverlay.csb node name (confirmed by static decode of the shipped layout).
static const char kResumeButton[] = "resumeButton-chinaonlylocalize";

extern "C" void ds4f_ui_probe_entry(uint64_t node, uint64_t layer)
{
    g_uip_node = node;
    g_uip_layer = layer;
    g_uip_child = 0;
    g_uip_slot_ok = 0;
    g_uip_seq++;
    if (!node || (node & 7ull)) return;
    uint64_t vt = *(volatile uint64_t *)(uintptr_t)node;
    if ((vt & 7ull) || vt < 0x100000000ull || vt >= 0x200000000ull) return;
    uint64_t fn = *(volatile uint64_t *)(uintptr_t)(vt + 0x210ull);
    if (fn < 0x100000000ull || fn >= 0x200000000ull) return;
    g_uip_slot_ok = 1;
    typedef void *(*get_child_fn)(void *, const std::string *);
    get_child_fn call = (get_child_fn)(uintptr_t)fn;
    std::string name(kResumeButton);
    g_uip_child = (uint64_t)(uintptr_t)call((void *)(uintptr_t)node, &name);
}
