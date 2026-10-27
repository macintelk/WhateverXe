#include "kern_display13_tc_lightup.hpp"
#include "kern_display13_tc_native.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <cstring>
#include <sys/mman.h>
using namespace Display13TC;
struct IO {
    unsigned count {},fail {},last {},reg {},value {};
    bool admission=true,posting=true;
    uint32_t portError {};
    bool ready(bool clock){return admission && (!clock || count>0) && (!fail || last!=fail);}
    void step(unsigned n){assert(n==++count);last=n;}
    void clocks(){step(1);}
    bool transClock(unsigned r,unsigned v){step(2);reg=r;value=v;return posting;}
    void plane(){step(3);}void scaler(){step(4);}void pipe(){step(5);}
    void signal(){step(6);}uint32_t port(){step(7);return portError;}
    void packets(){step(8);}
};
struct DPIO {
    unsigned count {},fail {};uint32_t trainingError {};bool admission=true;
    bool ready(bool clock){return admission && (!clock || count>0) && count!=fail;}
    void step(unsigned n){assert(n==++count);}
    void clocks(){step(1);}uint32_t train(){step(2);return trainingError;}
    bool transClock(unsigned r,unsigned v){step(3);assert(r>=0x46140 && r<=0x46148 && v>=0x60000000);return count!=fail;}
    void plane(){step(4);}void scaler(){step(5);}void pipe(){step(6);}void packets(){step(7);}
};
int main(){
    for(unsigned ddi=3;ddi<=6;++ddi)for(unsigned pipe=0;pipe<3;++pipe){
        IO io;const auto r=lightUpTMDS(io,ddi,pipe);
        assert(!r.status && r.stage==LightStage::Complete && io.count==8);
        assert(io.reg==0x46140+4*pipe && io.value==(ddi+3)<<28);
    }
    for(unsigned fail=1;fail<=8;++fail){
        IO io;io.fail=fail;if(fail==2)io.posting=false;
        const auto r=lightUpTMDS(io,3,0);
        assert(r.status && io.count==fail && unsigned(r.stage)==fail);
    }
    for(unsigned ddi: {0U,2U,7U,~0U}){IO io;assert(lightUpTMDS(io,ddi,0).status && !io.count);}
    IO unbound;unbound.admission=false;assert(lightUpTMDS(unbound,3,0).status && !unbound.count);
    IO pipe;assert(lightUpTMDS(pipe,3,3).status && !pipe.count);
    IO port;port.portError=0xe00002d6;const auto r=lightUpTMDS(port,3,0);
    assert(r.status==port.portError && r.stage==LightStage::Port && port.count==7);
    unsigned notify=0;assert(finishMode([](){return 0U;},[&](){++notify;})==0 && notify==0);
    assert(finishMode([](){return 0xe00002d8U;},[&](){++notify;})==0xe00002d8U && notify==1);

    for(unsigned ddi=3;ddi<=6;++ddi)for(unsigned pipe=0;pipe<3;++pipe){
        DPIO io;io.fail=99;const auto r=lightUpDP(io,ddi,pipe);
        assert(!r.status && r.stage==LightStage::Complete && io.count==7);
    }
    const LightStage dpStages[]={LightStage::Binding,LightStage::Clocks,LightStage::Training,LightStage::TransClock,
        LightStage::Plane,LightStage::Scaler,LightStage::Pipe,LightStage::Packets};
    for(unsigned fail=1;fail<=7;++fail){DPIO io;io.fail=fail;const auto r=lightUpDP(io,5,1);
        assert(r.status && r.stage==dpStages[fail] && io.count==fail);}
    for(auto error:{1U,0xe00002d6U,0xe00002d8U}){DPIO io;io.fail=99;io.trainingError=error;
        const auto r=lightUpDP(io,5,1);assert(r.status==error && io.count==2 && r.stage==LightStage::Training);}
    DPIO rejected;assert(lightUpDP(rejected,5,0).status && !rejected.count);

    // Execute the actual x86 patch bytes with both native flag outcomes.
    // Error keeps eax and skips all post-mode work. Success reaches the same
    // flag-dependent destinations as the donor. No real driver code executes.
    auto mem=static_cast<unsigned char *>(mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));
    assert(mem!=MAP_FAILED);memset(mem,0xcc,4096);
    const unsigned char prefix[]={0x41,0x56,0x49,0x89,0xfe,0x89,0xf0}; // save r14; r14=rdi; eax=esi
    memcpy(mem,prefix,sizeof(prefix));memcpy(mem+sizeof(prefix),Native::outerAfter,sizeof(Native::outerAfter));
    const unsigned char yes[]={0xb8,1,0,0,0,0x41,0x5e,0xc3},no[]={0xb8,2,0,0,0,0x41,0x5e,0xc3};
    const unsigned char error[]={0x41,0x5e,0xc3};
    memcpy(mem+sizeof(prefix)+sizeof(Native::outerAfter),yes,sizeof(yes));
    memcpy(mem+sizeof(prefix)+0x50,no,sizeof(no));
    memcpy(mem+sizeof(prefix)+Native::modeExitOffset-Native::outerBeforeOffset,error,sizeof(error));
    // Inner adapter: eax input -> r13d -> eax, with the callee-saved register restored.
    const unsigned char innerPrefix[]={0x41,0x55,0x89,0xf8};
    const unsigned char innerExit[]={0x44,0x89,0xe8,0x41,0x5d,0xc3};
    memcpy(mem+1024,innerPrefix,sizeof(innerPrefix));memcpy(mem+1028,Native::innerAfter,sizeof(Native::innerAfter));
    memcpy(mem+1031,innerExit,sizeof(innerExit));
    assert(!mprotect(mem,4096,PROT_READ|PROT_EXEC));
    auto outer=reinterpret_cast<unsigned (*)(void *,unsigned)>(mem);
    auto inner=reinterpret_cast<unsigned (*)(unsigned)>(mem+1024);
    unsigned char controller[0xc60]{};
    for(unsigned flag=0;flag<2;++flag){controller[0xc5c]=flag;
        assert(outer(controller,0)==(flag?1U:2U));
        for(auto error: {1U,0xe00002d8U,0xe00002d6U})assert(outer(controller,error)==error && inner(error)==error);
    }
    assert(inner(0)==0);munmap(mem,4096);
    puts("PASS TC LightUp: clock/each-stage failure stop, 12 selectors, native error branches, balanced notification");
}
