// Fixed-TMDS ordering for the exact Display13 native LightUpTMDS adapter.
// The caller's hwSetMode performs disableDisplay on any returned error.
#ifndef KERN_DISPLAY13_TC_LIGHTUP_HPP
#define KERN_DISPLAY13_TC_LIGHTUP_HPP
#include <stdint.h>
namespace Display13TC {
enum class LightStage : uint32_t {Binding,Clocks,TransClock,Plane,Scaler,Pipe,Signal,Port,Packets,Complete,Training};
struct LightResult {LightStage stage {LightStage::Binding};uint32_t status {0xe00002d8U};};
template<class IO> LightResult lightUpTMDS(IO &io,unsigned ddi,unsigned pipe) {
    LightResult r;
    if(ddi<3 || ddi>6 || pipe>=3 || !io.ready(false))return r;
    r.stage=LightStage::Clocks;io.clocks();
    if(!io.ready(true))return r;
    r.stage=LightStage::TransClock;
    if(!io.transClock(0x46140+4*pipe,(ddi+3)<<28))return r;
    r.stage=LightStage::Plane;io.plane();if(!io.ready(true))return r;
    r.stage=LightStage::Scaler;io.scaler();if(!io.ready(true))return r;
    r.stage=LightStage::Pipe;io.pipe();if(!io.ready(true))return r;
    r.stage=LightStage::Signal;io.signal();if(!io.ready(true))return r;
    r.stage=LightStage::Port;
    const auto enabled=io.port();if(enabled){r.status=enabled;return r;}
    if(!io.ready(true))return r;
    r.stage=LightStage::Packets;io.packets();if(!io.ready(true))return r;
    r.stage=LightStage::Complete;r.status=0;return r;
}
// Fixed SST retains native training and SDP setup, but stops on failure
// instead of the donor's unconditional success/pipe programming afterward.
template<class IO> LightResult lightUpDP(IO &io,unsigned ddi,unsigned pipe) {
    LightResult r;
    if(ddi<3 || ddi>6 || pipe>=3 || !io.ready(false))return r;
    r.stage=LightStage::Clocks;io.clocks();if(!io.ready(true))return r;
    r.stage=LightStage::Training;
    const auto trained=io.train();if(trained){r.status=trained;return r;}
    if(!io.ready(true))return r;
    r.stage=LightStage::TransClock;if(!io.transClock(0x46140+4*pipe,(ddi+3)<<28))return r;
    r.stage=LightStage::Plane;io.plane();if(!io.ready(true))return r;
    r.stage=LightStage::Scaler;io.scaler();if(!io.ready(true))return r;
    r.stage=LightStage::Pipe;io.pipe();if(!io.ready(true))return r;
    r.stage=LightStage::Packets;io.packets();if(!io.ready(true))return r;
    r.stage=LightStage::Complete;r.status=0;return r;
}
// Native hwSetMode sends the begin notification before the path call. Its
// error branch skips post-mode writes and the normal end notification;
// balance that notification without replacing the returned error.
template<class Run,class Notify> uint32_t finishMode(Run run,Notify notify) {
    const auto result=run();if(result)notify();return result;
}
}
#endif
