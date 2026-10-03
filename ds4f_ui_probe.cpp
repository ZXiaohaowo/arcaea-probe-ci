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
#include <memory>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <pthread.h>
#include <mach-o/dyld.h>
#include "practice_rate_ui.h"
#include "ds4f_native_ui_profile.h"

extern "C" {
volatile uint64_t g_uip_node, g_uip_layer, g_uip_child, g_uip_seq;
volatile int g_uip_slot_ok;
volatile int g_uip_install;
volatile uint64_t g_uip_practice, g_uip_fake;
void pcp_rate_ui_open(void);
}

namespace {

constexpr auto kLoaderRva = ds4f_ui_7256::loader;
constexpr auto kSetStringRva = ds4f_ui_7256::set_text;
constexpr auto kAddTouchRva = ds4f_ui_7256::add_touch;
constexpr auto kSetPosSlot = ds4f_ui_7256::set_position_slot;
constexpr auto kAddChildSlot = ds4f_ui_7256::add_child_slot;
constexpr auto kGetChildSlot = ds4f_ui_7256::get_child_slot;

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

void setText(void *node, const char *value)
{
    std::string text(value);
    auto base=(uintptr_t)_dyld_get_image_header(0);
    ((SetStringFn)(base+kSetStringRva))(node,&text);
}

void listen(void *node, const std::function<void(void *,int)> &callback)
{
    auto base=(uintptr_t)_dyld_get_image_header(0);
    ((AddTouchFn)(base+kAddTouchRva))(node,&callback);
}

// One context per pause overlay. Callbacks own this context, which only BORROWS
// its engine nodes. No timer or asynchronous task keeps any native node pointer.
// No engine retain/release is issued: loader + parent ownership follows v27a.
struct NativePanel {
    void *overlay{}, *entry{}, *panel{}, *value{}, *hint{};
    uint64_t generation{}, epoch{};
    unsigned draft{100};
    bool open{};
    bool current() const { return pthread_main_np() && generation==g_uip_seq; }
    void row(bool show) {
        static const char *names[]={"resumeButton-chinaonlylocalize",
            "retryButton-chinaonlylocalize","quitButton-chinaonlylocalize"};
        const float xs[]={256,512,1024};
        for(int i=0;i<3;i++) setPos(getChild(overlay,names[i]),xs[i],show?300.0f:-30000.0f);
        setPos(entry,768,show?300.0f:-30000.0f);
        setPos(getChild(overlay,"pauseText"),640,show?366.38f:-30000.0f);
    }
    void refresh() {
        char text[32]; std::snprintf(text,sizeof(text),"%.2fx",draft/100.0);
        setText(value,text);
        setText(hint,"0.50x - 2.00x | +/-0.05 / 0.01");
    }
    void close() {
        if(!current()) return;
        open=false; setPos(panel,0,-30000); row(true);
    }
};

bool buildPanel(const std::shared_ptr<NativePanel> &p)
{
    auto base=(uintptr_t)_dyld_get_image_header(0);
    std::string path(ds4f_ui_7256::panel_resource);
    void *panel=((LoadFn)(base+kLoaderRva))(&path);
    if(!panel) return false;
    const char *names[]={"hs","hint_text","title_text","set_text",
        "left_button","left_button_small","right_button_small","right_button",
        "set_button","button_close"};
    void *nodes[10]{};
    for(int i=0;i<10;i++) if(!(nodes[i]=getChild(panel,names[i]))) return false;
    auto add=(AddChildFn)vslot(p->overlay,kAddChildSlot);
    if(!add) return false;
    p->panel=panel;p->value=nodes[0];p->hint=nodes[1];
    setText(nodes[2],"Practice");setText(nodes[3],"Apply");
    const int steps[]={-5,-1,1,5};
    const float xs[]={260,420,860,1020};
    for(int i=0;i<4;i++) {
        setPos(nodes[4+i],xs[i],480);
        const int delta=steps[i];
        listen(nodes[4+i],[p,delta](void *,int event) {
            if(event!=2 || !p->current() || !p->open) return;
            auto s=pcp_rate_ui_state();
            if(!s.visible || s.epoch!=p->epoch || s.pending) return;
            p->draft=(unsigned)std::clamp((int)p->draft+delta,
                (int)PCP_RATE_MIN_PERCENT,(int)PCP_RATE_MAX_PERCENT);
            p->refresh();
        });
    }
    listen(nodes[8],[p](void *,int event) {
        if(event!=2 || !p->current() || !p->open) return;
        if(pcp_rate_ui_request(p->draft,p->epoch)) {
            pcp_rate_ui_save_percent(p->draft);p->close();
        } else {setText(p->hint,"Not ready - close and pause again");}
    });
    listen(nodes[9],[p](void *,int event) {if(event==2) p->close();});
    setPos(panel,0,-30000);add(p->overlay,panel);
    return true;
}

void openPanel(const std::shared_ptr<NativePanel> &p)
{
    if(!p->current()) return;
    auto state=pcp_rate_ui_state();
    if(!state.visible || state.pending) return;
    if(!p->panel && !buildPanel(p)) {pcp_rate_ui_open();return;}
    p->epoch=state.epoch;p->draft=pcp_rate_ui_stored_percent();p->refresh();
    p->row(false);
    // Resource coordinates are 1280x960; the pause resource is 1280x720.
    setPos(p->panel,0,-120);p->open=true;
}

}  // namespace

extern "C" void ds4f_ui_probe_entry(uint64_t node, uint64_t layer)
{
    if(!pthread_main_np()) return;
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
    if(std::memcmp((void *)(base+kLoaderRva),ds4f_ui_7256::loader_bytes,8) ||
       std::memcmp((void *)(base+kSetStringRva),ds4f_ui_7256::text_bytes,8) ||
       std::memcmp((void *)(base+kAddTouchRva),ds4f_ui_7256::touch_bytes,8)) {
        g_uip_install=-16;return;
    }

    std::string path(ds4f_ui_7256::pause_resource);
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
    setPos(practice, 768.0f, 300.0f);

    /* four-slot native row: 256 / 512 / 768 (practice) / 1024 */
    if (void *r = getChild(overlay, "resumeButton-chinaonlylocalize")) setPos(r, 256.0f, 300.0f);
    if (void *r = getChild(overlay, "retryButton-chinaonlylocalize")) setPos(r, 512.0f, 300.0f);
    if (void *r = getChild(overlay, "quitButton-chinaonlylocalize")) setPos(r, 1024.0f, 300.0f);

    auto panel=std::make_shared<NativePanel>();
    panel->overlay=overlay;panel->entry=practice;panel->generation=g_uip_seq;
    std::function<void(void *, int)> cb = [panel](void *, int type) {
        if (type == 2) openPanel(panel);
    };
    ((AddTouchFn)(base + kAddTouchRva))(practice, &cb);

    auto addChild = (AddChildFn)vslot(overlay, kAddChildSlot);
    if (!addChild) { g_uip_install = -15; return; }
    addChild(overlay, fake);

    g_uip_practice = (uint64_t)(uintptr_t)practice;
    g_uip_install = 1;
}
