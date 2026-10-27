// Hardware coordinates and donor capacity are separate from firmware wiring.
// A resource description does not grant GPU, protocol or live-owner admission.
#ifndef KERN_XELP_DISPLAY_RESOURCES_HPP
#define KERN_XELP_DISPLAY_RESOURCES_HPP
#include "kern_xelp_devices.hpp"
namespace XELP {
// Select an implemented Display adapter independently of GT admission. Linux
// v6.12 intel_display_device.c binds ADL-S and RPL-S to adl_s_desc; intel_bios.c
// consequently uses the same PHY-based VBT mapping for both. This selector
// grants no physical identity, stepping, fuse or donor qualification.
enum class DisplayAdapter : uint32_t { Unknown, TGL, ADLS, ADLN, ADLP };
constexpr DisplayAdapter displayAdapter(Family family) {
    switch(family) {
        case Family::TigerLake:return DisplayAdapter::TGL;
        case Family::AlderLakeS:
        case Family::RaptorLakeS:return DisplayAdapter::ADLS;
        case Family::AlderLakeP:
        case Family::RaptorLakeP:return DisplayAdapter::ADLP;
        case Family::AlderLakeN:return DisplayAdapter::ADLN;
        default:return DisplayAdapter::Unknown;
    }
}
struct ADLSComboResources {
    // Linux v6.12: ADL-S Combo PHY A..E map to DDI A, TC1..TC4.
    static constexpr uint32_t physicalCount=5;
    static constexpr uint32_t ddi(uint32_t phy) {return phy<physicalCount ? (phy?phy+2:0) : 9;}
    static constexpr uint32_t phy(uint32_t port) {return port==0?0:port>=3 && port<=6?port-2:physicalCount;}
    static constexpr uint32_t physicalMask() {
        uint32_t mask=0;for(uint32_t i=0;i<physicalCount;++i)mask|=1U<<ddi(i);return mask;
    }
};
struct DisplayDonorCapacity {
    // Exact C004FC1A enableClocks/disableClocks use three Combo entries.
    // A fourth physical PLL register does not extend these native tables.
    static constexpr uint32_t comboSlots(Donor donor) {return donor==Donor::C004FC1A?3:0;}
};
struct ADLSComboBackend {
    static constexpr uint32_t mask(Donor donor) {
        uint32_t mask=0;
        for(uint32_t i=0;i<ADLSComboResources::physicalCount;++i)
            if(i<DisplayDonorCapacity::comboSlots(donor))mask|=1U<<ADLSComboResources::ddi(i);
        return mask;
    }
    static constexpr bool clockSlot(Donor donor,uint32_t port,uint32_t &slot) {
        slot=ADLSComboResources::phy(port);
        if(slot<DisplayDonorCapacity::comboSlots(donor))return true;
        slot=~0U;return false;
    }
};
}
#endif
