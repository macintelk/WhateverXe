#include "kern_alderlake_n.hpp"
#include <vector>
#include <cassert>
#include <cstdio>
struct IOMemoryMap {
    std::vector<uint32_t> memory=std::vector<uint32_t>(0x400000);
    uintptr_t getVirtualAddress() const {return reinterpret_cast<uintptr_t>(memory.data());}
    uint64_t getLength() const {return 0x1000000;}
    uint32_t &at(uint32_t reg) {return memory[reg/4];}
};
void IODelay(unsigned) {}
// PRODUCTION_IO
using namespace Display13Power;
using namespace AlderLakeN;
static void firmware(IOMemoryMap &map,unsigned ddi,bool request) {
    map.at(Driver)=0xFC0F;map.at(DDI)=15;map.at(Fuses)=0x8E380000;
    map.at(AUX)=(request?3U:1U)<<(2*ddi);
    map.at(0x60400)=0x82030006|((ddi+1)<<27);map.at(0x70008)=0xC0000000;
    map.at(0x64000+ddi*0x100)=0x80000006;map.at(0x46140)=(ddi+1)<<28;
    map.at(0x60540)=0x80040300;map.at(0x60030)=0x7E100000;
    map.at(0x60034)=map.at(0x60044)=0x800000;map.at(0x60040)=0x100000;
}
struct FaultIO:Display13FBBootIO {
    using Display13FBBootIO::Display13FBBootIO;
    bool loseSource {},loseRequest {};
    void write(uint32_t reg,uint32_t value) {
        Display13FBBootIO::write(reg,value);
        if(reg==AUX) {
            if(loseSource)map->at(0x60540)^=1;
            if(loseRequest)map->at(AUX)&=~10U;
        }
    }
};
int main() {
    unsigned cases=0;
    for(unsigned ddi=0;ddi<2;++ddi)for(bool requested:{false,true})for(bool peer:{false,true}) {
        IOMemoryMap map;firmware(map,ddi,requested);const auto other=1-ddi;
        if(peer)map.at(AUX)|=3U<<(2*other);
        const auto initial=map.at(AUX);Display13FBBootIO io{&map};NativeBridge bridge;MainLinkAUX link;
        assert(bridge.begin(io,ddi,3).status==Status::Complete);
        const auto snapshot=readDisplay(io,true);assert(snapshot.status==ReadoutStatus::Complete && snapshot.count==157);
        const auto heldResult=holdDPBootAUX(io,snapshot,bridge,link,ddi);
        assert(heldResult.status==Status::Complete && heldResult.writes==(requested?0U:1U));
        assert(!link.owner && link.bootPending && link.port==ddi && !bridge.nativeAUXRefs(ddi));
        assert(link.lease.owner==&bridge.state && bridge.state.refs[ddi?6:8]==1);
        assert(link.begin(io,bridge,7,ddi).status==Status::NotReady && link.end(io,bridge,7,ddi).status==Status::NotReady);
        assert(holdDPBootAUX(io,readDisplay(io,true),bridge,link,ddi).status==Status::NotReady);
        assert(bridge.getAUX(io,ddi).status==Status::Complete && bridge.putAUX(io,ddi).status==Status::Complete);
        assert(bridge.state.refs[ddi?6:8]==1 && !bridge.nativeAUXRefs(ddi));
        const auto after=readDisplay(io,true);const auto requestBefore=map.at(AUX);
        assert(adoptDPBootAUX(io,after,bridge,link,7,ddi).status==Status::Complete);
        assert(link.owner==7 && !link.bootPending && !bridge.nativeAUXRefs(ddi) && map.at(AUX)==requestBefore);
        assert(adoptDPBootAUX(io,after,bridge,link,8,ddi).status==Status::NotReady);
        assert(bridge.adoptBootPath(9,0,ddi).status==Status::Complete);
        assert(link.begin(io,bridge,7,ddi).status==Status::Complete);
        assert(link.end(io,bridge,7,ddi).status==Status::NotReady); // Live GOP cannot release.
        map.at(0x64000+ddi*0x100)=0x86;
        assert(link.end(io,bridge,7,ddi).status==Status::Complete && !link.owner && !link.lease.owner);
        assert(map.at(AUX)==initial); // Borrowed firmware/peer requests survive; only our request clears.
        assert(bridge.putDDI(io,ddi).status==Status::Complete && bridge.putPG(io,1).status==Status::Complete);
        for(auto refs:bridge.state.refs)assert(!refs);
        io.write(0x60540,0);assert(map.at(0x60540)==0x80040300); // Boot IO cannot write link registers.
        assert(!io.readable(0x1000000) && !Display13FBBootIO{nullptr}.readable(0));
        ++cases;
    }
    for(unsigned fault=0;fault<9;++fault) {
        IOMemoryMap map;firmware(map,0,false);Display13FBBootIO io{&map};NativeBridge b;MainLinkAUX l;
        assert(b.begin(io,0,3).status==Status::Complete);auto boot=readDisplay(io,true);
        if(fault==0)map.at(AUX)=0;
        if(fault==1)map.at(0x60540)=0x80000000;
        if(fault==2)b.bootPGProtected=false;
        if(fault==3)b.bootDDIProtected=false;
        if(fault==4)b.bootAdopted=true;
        if(fault==5)b.bootPort=1;
        if(fault==6)b.ddiRefs[0]=0;
        if(fault==7)b.state.suspended=true;
        if(fault==8)b.state.faulted=true;
        assert(holdDPBootAUX(io,boot,b,l,0).status==Status::NotReady && !l.lease.owner && !l.bootPending);
    }
    for(unsigned fault=0;fault<7;++fault) {
        IOMemoryMap map;firmware(map,0,false);Display13FBBootIO io{&map};NativeBridge b;MainLinkAUX l;
        assert(b.begin(io,0,3).status==Status::Complete && holdDPBootAUX(io,readDisplay(io,true),b,l,0).status==Status::Complete);
        auto boot=readDisplay(io,true);uintptr_t target=7;unsigned port=0;
        if(fault==0)target=0;if(fault==1)port=1;if(fault==2)map.at(AUX)=0;
        if(fault==3)map.at(0x60540)^=1;if(fault==4)b.bootPGProtected=false;
        if(fault==5)b.bootAdopted=true;if(fault==6)b.state.suspended=true;
        assert(adoptDPBootAUX(io,boot,b,l,target,port).status==Status::NotReady && l.bootPending && !l.owner && l.lease.owner);
    }
    {IOMemoryMap map;firmware(map,0,false);FaultIO io{&map};NativeBridge b;MainLinkAUX l;
     assert(b.begin(io,0,3).status==Status::Complete);io.loseSource=true;
     assert(holdDPBootAUX(io,readDisplay(io,true),b,l,0).status==Status::RequestLost);
     assert(b.state.faulted && l.bootPending && l.lease.owner && map.at(AUX)==3 && !b.nativeAUXRefs(0));}
    {IOMemoryMap map;firmware(map,0,false);FaultIO io{&map};NativeBridge b;MainLinkAUX l;
     assert(b.begin(io,0,3).status==Status::Complete);io.loseRequest=true;
     assert(holdDPBootAUX(io,readDisplay(io,true),b,l,0).status==Status::RequestLost);
     assert(b.state.faulted && !l.bootPending && !l.lease.owner && map.at(AUX)==1);}
    printf("PASS DP boot AUX: %u real-IO ownership lifecycles, transaction independence, refusal/fault retention and power-only writes\n",cases);
}
