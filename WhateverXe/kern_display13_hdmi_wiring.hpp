// The legacy explicit HDMI property selects Combo wiring only.
// Native nine-port identity and three-pipe resources are managed separately.
// Firmware-selected Display13 routes also support fixed TC HDMI.
#ifndef KERN_DISPLAY13_HDMI_WIRING_HPP
#define KERN_DISPLAY13_HDMI_WIRING_HPP
// Compatibility name for the version1 HDMI property and older callers.
#include "kern_display13_output_wiring.hpp"
using Display13HDMIWiring = Display13OutputWiring;
#endif
