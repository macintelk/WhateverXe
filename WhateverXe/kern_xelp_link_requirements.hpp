// External HDMI/fixed-DP requirements, after platform/wiring admission.
// Coordinate mapping and generation-specific power/ownership stay in adapters.
#ifndef KERN_XELP_LINK_REQUIREMENTS_HPP
#define KERN_XELP_LINK_REQUIREMENTS_HPP
#include "kern_xelp_devices.hpp"
namespace XELP {
struct LinkRequirements {
 enum class PHY {None,Combo,TC};
 enum class Protocol {None,HDMI,FixedDP};
 Display display {Display::Unknown};PHY phy {PHY::None};Protocol protocol {Protocol::None};
 bool valid() const {return (display==Display::XeD || display==Display::XeLPD) &&
  phy!=PHY::None && protocol!=Protocol::None;}
 // Linux v6.12 intel_wait_ddi_buf_active: Display12/13 share these limits.
 // The native loop reads before each 5us delay; the deadline itself times out.
 uint32_t activeWaitUS() const {return valid() ? (phy==PHY::TC?3000:1000) : 500;}
 enum class Preparation {None,NativeCombo,NativeTC12,ColdTC13,FirmwareTC13};
 // The evidence is supplied by a powered, identity-bound host readout. A mode
 // number, HPD bit or an ownership bit alone is never firmware evidence.
 Preparation preparation(bool verifiedFirmware) const {
  if(!valid())return Preparation::None;
  if(phy==PHY::Combo)return Preparation::NativeCombo;
  // Display12 native mode owns AUX -> FIA. Display13 must acquire access ->
  // ownership -> AUX before base mode. Their ordering must stay distinct.
  if(display==Display::XeD)return Preparation::NativeTC12;
  return verifiedFirmware?Preparation::FirmwareTC13:Preparation::ColdTC13;
 }
 uint32_t activePolls() const {return activeWaitUS()/5;}
};
}
#endif
