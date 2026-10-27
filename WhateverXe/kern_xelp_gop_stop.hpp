// Shared Display12/13 GOP retirement primitives. Admission, protected peers,
// primary-pipe policy and PHY/DDB release remain with the owning backend.
#ifndef KERN_XELP_GOP_STOP_HPP
#define KERN_XELP_GOP_STOP_HPP
#include <stdint.h>
namespace XELPGOP {
// Preserve the caller's diagnostic value, including changes made by stable().
// Display13's frame-drain deadline omits the final delay; register waits do not.
template<class IO,class Stable,class Ready,class Status>
bool poll(IO &io,Stable &stable,Ready ready,uint32_t reg,uint32_t &value,
          Status &status,Status drift,bool delayOnTimeout=true) {
    for(unsigned n=0;n<1000;++n) {
        value=io.read(reg);
        if(value==UINT32_MAX || !stable()){status=drift;return false;}
        if(ready(value))return true;
        if(n<999 || delayOnTimeout)io.delay();
    }
    status=Status::Timeout;return false;
}
// Linux intel_ddi_disable_transcoder_func / disable_ddi_buf: the transcoder
// has stopped before entry. Keep shared PLLs/power held until buffer idle.
template<class Write,class Wait>
bool stopBuffer(Write &write,Wait &wait,uint32_t bank,uint32_t bufferReg,
                uint32_t buffer,uint32_t transport,bool dp) {
    if(!write(bufferReg,buffer&~0x80000000U))return false;
    if(dp && (!write(0x60540+bank,transport&~0x80000000U) ||
              !wait(0x60540+bank,0x80000000U,0)))return false;
    return wait(bufferReg,0x80000080U,0x80);
}
}
#endif
