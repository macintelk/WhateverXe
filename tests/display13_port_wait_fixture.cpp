#include "kern_xelp_port_wait.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
#include <sys/mman.h>
using namespace XELPPortWait;
// PRODUCTION_HARNESS
alignas(8) uint8_t port[0x700]{};
uint32_t *config,buffer,baseResult;
unsigned writes,reads,delays,baseCalls,nowUS,ackUS;
bool unreadable;
uintptr_t expectedWritePC;
uint32_t read(void *p,uint32_t reg) {
    assert(p==port && reg==0x64000+256*config[0]);++reads;
    if(unreadable && writes)return ~0U;
    if(writes && nowUS>=ackUS)buffer&=~0x80U;
    return buffer;
}
void write(void *p,uint32_t reg,uint32_t value) {
    assert(p==port && reg==0x64000+256*config[0]);
    assert(reinterpret_cast<uintptr_t>(__builtin_return_address(0))==expectedWritePC);
    ++writes;buffer=value;
}
uint32_t base(void *p) {assert(p==port);++baseCalls;return baseResult;}
void logMock(const char *,...) {}
void delay(unsigned us) {assert(us==5);nowUS+=us;++delays;assert(nowUS<=3000);}
void jump(uint8_t *p,uintptr_t target) {p[0]=0x48;p[1]=0xb8;memcpy(p+2,&target,8);p[10]=0xff;p[11]=0xe0;}
void redirect(uint8_t *image,unsigned site,unsigned stub) {assert(image[site]==0xe8);int32_t rel=stub-site-5;memcpy(image+site+1,&rel,4);}
template<class T>void field(unsigned off,T value){memcpy(port+off,&value,sizeof(value));}
int main(int argc,char **argv) {
    assert(argc==2);std::ifstream f(argv[1],std::ios::binary);std::vector<char>b((std::istreambuf_iterator<char>(f)),{});
    assert(b.size()>0x5fc55 && waitKnown(reinterpret_cast<uint8_t*>(b.data())+WaitOffset,sizeof(WaitBefore)));
    // Every non-relocation byte is guarded, including loop-back and epilogue branches.
    for(unsigned i=0;i<sizeof(WaitBefore);++i) {
        auto changed=b;changed[WaitOffset+i]^=1;
        const bool relocation=(i>=44&&i<48)||(i>=76&&i<80);
        assert(waitKnown(reinterpret_cast<uint8_t*>(changed.data())+WaitOffset,sizeof(WaitBefore))==relocation);
    }
    unsigned lateAccepted=0;
    for(bool patched:{false,true}) {
        auto image=static_cast<uint8_t*>(mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0));
        assert(image!=MAP_FAILED && b.size()<0x400000);memcpy(image,b.data(),b.size());
        if(patched) {
            uint8_t after[sizeof(WaitBefore)];patch(after,image+WaitOffset,reinterpret_cast<uintptr_t>(XE::framebufferPortWaitPolls));
            // Only initialization/profiling bytes and loop-back displacement change.
            for(unsigned i=19;i<sizeof(after);++i)if(i!=51)assert(after[i]==image[WaitOffset+i]);
            memcpy(image+WaitOffset,after,sizeof(after));
        }
        *reinterpret_cast<uintptr_t*>(image+0xc95a0)=reinterpret_cast<uintptr_t>(base);
        jump(image+0x1fe8e,reinterpret_cast<uintptr_t>(read));jump(image+0x1fea8,reinterpret_cast<uintptr_t>(write));
        jump(image+0x300000,reinterpret_cast<uintptr_t>(logMock));jump(image+0x300020,reinterpret_cast<uintptr_t>(delay));
        for(unsigned site:{0x5fa33U,0x5fac4U,0x5fbebU,0x5fc4eU})redirect(image,site,0x300000);
        redirect(image,0x5fbcb,0x300020);
        auto native=reinterpret_cast<uint32_t(*)(void*)>(image+0x5f9f4);
        expectedWritePC=reinterpret_cast<uintptr_t>(image+WaitOffset);
        for(unsigned platform=0;platform<3;++platform)
        for(unsigned ddi:{0U,1U,3U,4U,5U,6U})for(unsigned protocol:{1U,2U})
        for(unsigned ack:{0U,495U,500U,600U,995U,1000U,2000U,2995U,3000U,~0U})
        for(unsigned fault=0;fault<3;++fault) {
            if(platform==1 && !(AlderLakeS::ComboBackendMask&(1u<<ddi)))continue;
            setup(platform,ddi,platform==0 && ddi>=3?1:protocol);
            memcpy(&config,selectionPort+0x548,sizeof(config));
            const unsigned mode=platform==1 || ddi<3?1:4;
            field(0x548,config+0);field(0x120,ddi);field(0x590,0x64000+256*ddi);field(0x550,mode);
            field(0x124,4U);field(0x128,20U);port[0x114]=1;
            writes=reads=delays=baseCalls=nowUS=0;buffer=0x80;
            baseResult=fault==1?42:0;unreadable=fault==2;ackUS=ack;
            const unsigned budget=patched?(platform==1 || ddi<3?1000:3000):500;
            const auto result=native(port);
            const auto expected=fault==1?42U:(fault==2||ack>=budget)?0xe00002d6U:0;
            assert(result==expected && baseCalls==1 && writes==(fault==1?0U:1U));
            assert(nowUS<=budget && delays==nowUS/5);
            if(expected==0xe00002d6U)assert(nowUS==budget && reads==1+budget/5);
            if(!fault && ack==0)assert(nowUS==0 && reads==2); // no unconditional delay
            if(patched && !fault && platform!=1 && ddi>=3 && ack==2000){assert(!result);++lateAccepted;}
        }
        munmap(image,0x400000);
    }
    assert(lateAccepted==16);
    puts("PASS original vs patched exact enablePort: late ACK, bounded timeout, base failure, all-ones, immediate ACK, preserved write PC and native return; MMIO/time mocked");
}
