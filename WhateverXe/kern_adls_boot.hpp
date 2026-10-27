// ADL-S firmware may scan out on several pipes; the donor adopts only the
// first enabled transcoder. Retire the other linear GOP consumers before
// native startup. Keep clocks, PLLs and power requests for native accounting.
// Ordering: Linux v6.12 intel_display.c / intel_ddi.c, Display12 SST disable.
#ifndef KERN_ADLS_BOOT_HPP
#define KERN_ADLS_BOOT_HPP
#include "kern_adls_clock.hpp"
#include "kern_xelp_gop_stop.hpp"
#include "kern_alderlake_n.hpp"
#include "kern_display13_fb_irq.hpp"
namespace AlderLakeS {
namespace Boot {
enum class Status:uint32_t {Unavailable,Unchanged,Complete,Topology,Drift,Timeout};
struct Result {
    uint32_t version=1;Status status=Status::Unavailable;
    uint32_t active {},primary=UINT32_MAX,parked {},stage {},writes {},reg {},value {};
};
template<class IO> Result prepare(IO &io,bool admitted,const Wiring &w) {
    Result r;
    if(!admitted || !w.valid || !w.selected())return r;
    AlderLakeN::DisplayReadout s;s.status=AlderLakeN::ReadoutStatus::Complete;
    const auto add=[&](uint32_t reg) {
        if(s.count>=160 || !io.readable(reg)){s.status=AlderLakeN::ReadoutStatus::Unavailable;return UINT32_MAX;}
        const auto v=io.read(reg);s.registers[s.count++]={reg,v};
        if(v==UINT32_MAX)s.status=AlderLakeN::ReadoutStatus::Unavailable;
        return v;
    };
    const auto power=add(0x45404);
    if(power==UINT32_MAX || !(power&1))return r;
    const uint32_t common[] {0x45450,0x45454,0x45444,0x164280,0x1642BC,0x46010,0x46014,0x46018,0x46030};
    for(auto reg:common)add(reg);
    const unsigned ddis[] {0,3,4,5,6};
    for(auto d:ddis)add(0x64000+0x100*d);
    uint32_t ports[3] {UINT32_MAX,UINT32_MAX,UINT32_MAX},used=0;
    for(unsigned p=0;p<3;++p) {
        const uint32_t mask=p==0?1:p==1?0x15:0x55;
        if((power&mask)!=mask)continue;
        s.poweredPipes|=1U<<p;
        const auto bank=0x1000*p;
        const auto conf=add(0x70008+bank),func=add(0x60400+bank);
        const uint32_t local[] {0x60000U,0x6000CU,0x6001CU,0x60404U,0x60540U,0x70080U,
                     0x70180U,0x70188U,0x7018CU,0x70190U,0x7019CU,0x701A4U,0x701ACU,
                     0x70280U,0x70380U,0x70480U,0x70580U};
        for(auto off:local)add(off+bank);
        add(0x46140+4*p);
        const unsigned scaler[] {0,4,0x10};
        for(unsigned n=0;n<2;++n)for(auto off:scaler)add(0x68170+0x800*p+0x100*n+off);
        for(unsigned n=0;n<3;++n)add(Display13FBIRQ::DSBControl(p,n));
        if((conf&0xC0000000U) || (func&0x80000000U)) {
            if((conf&0xC0000000U)!=0xC0000000U || !(func&0x80000000U)) {r.status=Status::Topology;return r;}
            s.activePipes|=1U<<p;
        }
    }
    r.active=s.activePipes;
    if(s.status!=AlderLakeN::ReadoutStatus::Complete || io.read(0x45404)!=power)return r;
    if(!r.active || !(r.active&(r.active-1))) {r.status=Status::Unchanged;return r;}
    r.status=Status::Topology;
    // This transaction covers external fixed Combo routes, not panel/FEC/MST.
    if(w.panel)return r;
    const auto get=[&](uint32_t reg){uint32_t v=UINT32_MAX;AlderLakeN::value(s,reg,v);return v;};
    for(unsigned p=0;p<3;++p)if(s.poweredPipes&(1U<<p)) {
        const auto bank=0x1000*p;
        if(get(0x70080+bank)&63)return r;
        for(unsigned n=(r.active&(1U<<p))?1:0;n<5;++n)if(get(0x70180+bank+0x100*n)&0x80000000U)return r;
        for(unsigned n=0;n<3;++n)if(get(Display13FBIRQ::DSBControl(p,n))&0x80000001U)return r;
        if(!(r.active&(1U<<p)))continue;
        if(r.primary==UINT32_MAX)r.primary=p; // same first-active choice as probeBootPipe
        const auto func=get(0x60400+bank),encoding=(func>>27)&15,protocol=(func>>24)&7;
        if(!encoding || protocol>2 || ((func>>20)&7) || get(0x60404+bank))return r;
        const auto d=encoding-1;Port port;
        if(!Port::resolve(d,port) || !(w.selected()&(1U<<d)) || (used&(1U<<d)) ||
           (protocol==2 ? !w.hasDP(d) : !(w.hdmi&(1U<<d))))return r;
        ports[p]=d;used|=1U<<d;
        const auto tp=get(0x60540+bank),buffer=get(port.buffer),clock=get(0x46140+4*p);
        if(!(buffer&0x80000000U) || (get(port.clock)&port.clockOff) ||
           (clock>>28)!=d+1 || (protocol==2 && (!(tp&0x80000000U) || (tp&(1U<<27)))))return r;
        const auto geometry=XELPGeometry::linear(get(0x70180+bank),get(0x70188+bank),get(0x70190+bank),get(0x7018C+bank),get(0x701A4+bank));
        if(!AlderLakeN::bootGeometry(s,p,geometry,get(0x60000+bank),get(0x6000C+bank)) ||
           (get(0x7019C+bank)&4095) || get(0x7019C+bank)!=get(0x701AC+bank))return r;
    }
    uint32_t changed[32] {},count=0,current=UINT32_MAX;
    const auto stable=[&] {
        for(unsigned i=0;i<s.count;++i) {
            const auto x=s.registers[i];bool skip=false;
            for(unsigned j=0;j<count;++j)if(changed[j]==x.offset)skip=true;
            // BUF_IS_IDLE is hardware status: stopping this pipe may assert
            // it before the explicit buffer-disable write. Keep checking the
            // enable/config bits and every peer's complete buffer value.
            const uint32_t mask=current<3 && r.stage>=2 && x.offset==0x64000+0x100*ports[current] ? ~0x80U : UINT32_MAX;
            if(!skip && ((io.read(x.offset)^x.value)&mask)){r.reg=x.offset;r.value=io.read(x.offset);return false;}
        }
        return true;
    };
    if(!stable()){r.status=Status::Drift;return r;}
    const auto write=[&](uint32_t reg,uint32_t v) {
        if(!stable()){r.status=Status::Drift;return false;}
        bool seen=false;for(unsigned i=0;i<count;++i)if(changed[i]==reg)seen=true;
        if(!seen){if(count>=32)return false;changed[count++]=reg;}
        io.write(reg,v);++r.writes;io.read(reg);return true;
    };
    const auto wait=[&](uint32_t reg,uint32_t mask,uint32_t value) {
        r.reg=reg;
        return XELPGOP::poll(io,stable,[&](uint32_t v){return (v&mask)==value;},
            reg,r.value,r.status,Status::Drift);
    };
    for(unsigned p=r.primary+1;p<3;++p)if(r.active&(1U<<p)) {
        const auto bank=0x1000*p;Port port;Port::resolve(ports[p],port);
        current=p;r.stage=1;
        const auto frame=io.read(0x70040+bank);
        if(frame==UINT32_MAX || !write(0x70180+bank,get(0x70180+bank)&~0x80000000U) ||
           !write(0x7019C+bank,get(0x7019C+bank)))return r;
        uint32_t now;
        if(!XELPGOP::poll(io,stable,[&](uint32_t v){return uint32_t(v-frame)>=2 && uint32_t(v-frame)<0x10000000U;},
            0x70040+bank,now,r.status,Status::Drift))return r;
        r.stage=2;
        if(!write(0x70008+bank,get(0x70008+bank)&~0x80000000U) || !wait(0x70008+bank,0xC0000000U,0))return r;
        r.stage=3;
        if(!write(0x60400+bank,get(0x60400+bank)&~(0x80000000U|(15U<<27)|(7U<<24))) ||
           !wait(0x60400+bank,0x80000000U,0))return r;
        for(unsigned n=0;n<2;++n) {
            const auto base=0x68170+0x800*p+0x100*n;
            if(get(base+0x10)&0x80000000U) {
                if(!write(base+0x10,0) || !wait(base+0x10,UINT32_MAX,0) || !write(base,0) || !write(base+4,0))return r;
            }
        }
        r.stage=4;
        if(!XELPGOP::stopBuffer(write,wait,bank,port.buffer,get(port.buffer),get(0x60540+bank),
            ((get(0x60400+bank)>>24)&7)==2))return r;
        r.stage=5;
        if(!write(0x46140+4*p,get(0x46140+4*p)&0x0FFFFFFFU) || !wait(0x46140+4*p,0xF0000000U,0))return r;
        r.parked|=1U<<p;
    }
    r.status=stable()?Status::Complete:Status::Drift;return r;
}
}
}
#endif
