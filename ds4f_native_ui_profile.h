#pragma once
#include <cstdint>
/* Native UI adapter for the verified 7.0.256 arm64 image only.
 * Keep version facts here; panel behaviour must not contain game offsets.
 * The runtime also checks the entry bytes before using these functions.
 */
namespace ds4f_ui_7256 {
constexpr uintptr_t loader = 0xF35924;
constexpr uintptr_t set_text = 0xE4C02C;
constexpr uintptr_t add_touch = 0xE10728;
constexpr unsigned char loader_bytes[] = {0xff,0x03,0x02,0xd1,0xf4,0x4f,0x06,0xa9};
constexpr unsigned char text_bytes[] = {0xf4,0x4f,0xbe,0xa9,0xfd,0x7b,0x01,0xa9};
constexpr unsigned char touch_bytes[] = {0x00,0x40,0x0d,0x91,0xb9,0xca,0xc8,0x17};
constexpr uintptr_t set_position_slot = 0x98;
constexpr uintptr_t add_child_slot = 0x1E8;
constexpr uintptr_t get_child_slot = 0x210;
constexpr const char *pause_resource = "layouts/ingame/PauseOverlay.csb";
constexpr const char *panel_resource = "layouts/songselect/HighspeedSettingLayer.csb";
}
