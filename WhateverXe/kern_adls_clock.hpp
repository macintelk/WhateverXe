// ADL-S compatibility names; shared ownership and translation live in XELP.
#ifndef KERN_ADLS_CLOCK_HPP
#define KERN_ADLS_CLOCK_HPP
#include "kern_alderlake_s.hpp"
#include "kern_xelp_combo_clock.hpp"
namespace AlderLakeS {
using ClockSnapshot=XELPComboClock::ClockSnapshot;
using ClockChoice=XELPComboClock::ClockChoice;
using ClockAllocation=XELPComboClock::ClockAllocation;
using ClockTranslation=XELPComboClock::ClockTranslation;
}
#endif
