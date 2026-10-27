// CPU portions of fixes for the shared c004fc1a donor. Caller owns admission.
#ifndef KERN_XELP_DONOR_HPP
#define KERN_XELP_DONOR_HPP
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace XELPDonor {
// Exact C004FC1A disablePowerWellDDI incorrectly derives C's request from
// TC1's refs/mode. Match the whole producer block and use C's own counter,
// as enablePowerWellDDI already does. Hardware/domain selection belongs to
// callers; do not discard an actually referenced DDI C on capable hardware.
constexpr uint32_t DDIReleaseOffset=0x5AD86;
static constexpr uint8_t DDIReleaseBefore[] {0x83,0x7b,0x3c,0x00,0x74,0x37,0x48,0xff,0x05,0x3d,0x6c,0x08,0x00,0x48,0xff,0x05,0xd6,0x70,0x08,0x00,0xbf,0x03,0x00,0x00,0x00,0xe8,0x46,0x04,0xfc,0xff,0x48,0x85,0xc0,0x74,0x1e,0x48,0xff,0x05,0xd0,0x70,0x08,0x00,0x48,0xff,0x05,0xa9,0x75,0x08,0x00,0x83,0xb8,0x50,0x05,0x00,0x00,0x02,0x0f,0x94,0xc0,0xeb,0x06,0x31,0xc0,0xeb,0x1b,0x31,0xc0,0x0f,0xb6,0xc0,0x48,0x01,0x05,0xa5,0x70,0x08,0x00,0x34,0x01,0x0f,0xb6,0xc0,0x48,0x01,0x05,0xe9,0x6b,0x08,0x00,0xc1,0xe0,0x05};
// xor eax,eax; cmp dword [rbx+38h],0; setne al; shl eax,5;
// jmp 5ADE2 (native union of all other DDI requests).
static constexpr uint8_t DDIReleaseAfter[] {0x31,0xc0,0x83,0x7b,0x38,0,0x0f,0x95,0xc0,0xc1,0xe0,5,0xe9,0x4b,0,0,0};
inline bool ddiReleaseSite(const uint8_t *image,size_t size) {
 return image && size>=DDIReleaseOffset+sizeof(DDIReleaseBefore) &&
  !memcmp(image+DDIReleaseOffset,DDIReleaseBefore,sizeof(DDIReleaseBefore));
}
inline bool displayPoolRequest(uint32_t enabled,uint32_t bytes) {
    return enabled && bytes && !(bytes&4095) && bytes<=0x4000000;
}
struct Poll {uint32_t value {},elapsedUs {},reads {};};
// hwSetPowerWellStatePG checks PG2/3/4 once, 1 us after the state ACK.
// Linux gen9_wait_for_power_well_fuses polls the actual fuse status instead.
// Match the complete donor read/test instructions, including the call target.
constexpr uint32_t fuseReadSites[] {0x39D,0x53F,0x6D3};
inline bool powerWellFuseSites(const uint8_t *function,size_t size) {
    static const uint8_t expected[][14] {
        {0xBE,0x00,0x20,0x04,0x00,0xE8,0x5D,0x29,0x00,0x00,0x0F,0xBA,0xE0,0x19},
        {0xBE,0x00,0x20,0x04,0x00,0xE8,0xBB,0x27,0x00,0x00,0x0F,0xBA,0xE0,0x18},
        {0xBE,0x00,0x20,0x04,0x00,0xE8,0x27,0x26,0x00,0x00,0x0F,0xBA,0xE0,0x17}
    };
    if(!function || size<fuseReadSites[2]+14)return false;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<14;++j)
        if(function[fuseReadSites[i]+j]!=expected[i][j])return false;
    return true;
}
inline uint32_t powerWellFuseMask(uintptr_t function,uintptr_t caller) {
    if(function && caller>=function)for(unsigned i=0;i<3;++i)
        if(caller-function==fuseReadSites[i]+10)return 1U<<(25-i);
    return 0;
}
template<class IO> Poll waitPowerWellFuse(IO &io,uint32_t initial,uint32_t mask) {
    Poll r {initial,0,0};
    while(mask && r.value!=UINT32_MAX && !(r.value&mask) && r.elapsedUs<1000) {
        io.delayUs(10);r.elapsedUs+=10;r.value=io.readStatus();++r.reads;
    }
    return r; // Never fabricate readiness or change a power request/refcount.
}
template<class IO> Poll waitEDIDReady(IO &io,uint32_t initial) {
    Poll r {initial,0,0};
    while(!(r.value&0xC00) && r.elapsedUs<50000) {
        io.delayUs(10);r.elapsedUs+=10;r.value=io.readStatus();++r.reads;
    }
    return r;
}
// computeLaneCount ignores arg0: setupOptimalLaneCount passes port+0x124,
// while the HDMI validator passes the actual port. Qualify the caller before
// even reading config; correcting only the result does not protect this ABI.
inline const uint32_t *hdmiLaneConfig(const void *argument,uintptr_t caller,uintptr_t validator) {
    if(!argument || !validator || caller!=validator)return nullptr;
    const uint32_t *config {};
    memcpy(&config,static_cast<const uint8_t *>(argument)+0x548,sizeof(config));
    return config;
}
inline bool skipHDMIDPLanes(bool selected,bool exactCaller,bool native,uint32_t available,uint32_t lanes) {
    return selected && exactCaller && !native && !available && !lanes;
}
}
#endif
