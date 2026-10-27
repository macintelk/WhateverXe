#include "kern_alderlake_s.hpp"
#include "kern_xelp_donor.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
#include <sys/mman.h>
#define FunctionCast(fn,p) reinterpret_cast<decltype(&fn)>(p)
struct XE {
 bool adlsDisplaySelected=true,adlsPowerReady=true;void *tglObservationController;
 AlderLakeS::Wiring adlsWiring;uintptr_t orgADLSPowerPG,orgTGLPowerDDIState;
 static uint8_t *adlsPowerTarget(void*);
 static void wrapADLSPowerPG(void*,bool,uint32_t);
 static void wrapTGLPowerDDIState(void*,bool,uint32_t);
};
XE state;XE *callbackXE=&state;
template<class...T>void tglTransitionRecord(T...){}
template<class...T>void tglObserveActivation(T...){}
template<class T>T &at(void*p,unsigned o){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+o);}
// PRODUCTION_WRAPPERS
alignas(8) uint8_t controller[0xF00]{},power[0x90]{},access[0x70]{},ports[9][0x600]{};
uintptr_t vtable[64]{};uint8_t *image;
struct Event {unsigned kind,value;};std::vector<Event> events;
unsigned requested,ackMissing,delays,errors,reads,physicalMask=0x79;
void pg(void *p,bool,unsigned mask){assert(p==power);events.push_back({0,mask});}
void *lookup(unsigned d){assert(d<9);return ports[d];}
void writeReg(void *p,unsigned long reg,unsigned value){
 assert(p==access && (reg==0x45450 || reg==0x45454));
 events.push_back({unsigned(reg),value});if(reg==0x45454)requested=value;
}
uint32_t readReg(void *p,unsigned long reg){
 assert(p==access && reg==0x45454);++reads;
 // Model only physical ADL-S IO wells. An unsupported request receives no
 // ACK; explicit fault injection removes a real port's ACK as well.
 unsigned ack=0;
 for(unsigned d=0;d<9;++d)if((physicalMask&(1U<<d)) && !(ackMissing&(1U<<d)))ack|=(requested>>(2*d+1)&1)<<(2*d);
 return requested|ack;
}
void delay(unsigned n){assert(n==1);++delays;}
void error(const char*,...){++errors;}
void jump(unsigned off,uintptr_t target){image[off]=0x48;image[off+1]=0xb8;memcpy(image+off+2,&target,8);image[off+10]=0xff;image[off+11]=0xe0;}
void redirect(unsigned site,unsigned stub,uintptr_t target,uint8_t opcode){assert(image[site]==opcode);jump(stub,target);int32_t rel=stub-site-5;memcpy(image+site+1,&rel,4);}
void setup(unsigned assignment){
 memset(power,0,sizeof(power));memset(controller,0,sizeof(controller));
 at<void*>(power,0x10)=controller;at<void*>(controller,0xC8)=power;
 at<void*>(power,0x78)=access;at<void*>(controller,0xC40)=access;
 XELP::VBT::Ports fw;fw.status=XELP::VBT::Status::Valid;
 const unsigned physical[]={0,3,4},hdmiDVO[]={0,1,2},dpDVO[]={10,7,8};
 unsigned hdmi=0,dp=0;
 for(unsigned n=0;n<3;++n){unsigned d=physical[n],kind=assignment%3;assignment/=3;
  if(kind){
   AlderLakeS::Port route;assert(AlderLakeS::Port::resolve(d,route));
   fw.present|=1U<<d;(kind==1?hdmi:dp)|=1U<<d;
   fw.port[d]={kind==1?0x60D2U:0x60D6U,kind==1?hdmiDVO[n]:dpDVO[n],n+1,kind==1?0:route.aux(),0xC0,0};
  }
  // Native Mode Combo=0 for either external HDMI or fixed DP. This is
  // independent of protocol in the firmware wiring definition.
  at<uint32_t>(ports[d],0x550)=0;
 }
 state.adlsWiring=AlderLakeS::Wiring::fromFirmware(fw);
 assert(state.adlsWiring.valid && state.adlsWiring.hdmi==hdmi && state.adlsWiring.dp==dp && !state.adlsWiring.excluded);
 events.clear();requested=ackMissing=delays=errors=reads=0;
}
unsigned expectedRequest(){unsigned out=0;for(unsigned d:{0u,3u,4u})if(at<uint32_t>(power,0x30+4*d))out|=2U<<(2*d);return out;}
int main(int argc,char **argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);std::vector<char>b((std::istreambuf_iterator<char>(f)),{});
 image=static_cast<uint8_t*>(mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0));assert(image!=MAP_FAILED);memcpy(image,b.data(),b.size());
 jump(0x1b1ea,(uintptr_t)lookup);jump(0x5e344,(uintptr_t)readReg);
 redirect(0x5ae05,0x300000,(uintptr_t)XE::wrapTGLPowerDDIState,0xe9);
 redirect(0x5c2a9,0x300000,(uintptr_t)XE::wrapTGLPowerDDIState,0xe9);
 redirect(0x5c460,0x300020,(uintptr_t)delay,0xe8);
 redirect(0x5c484,0x300040,(uintptr_t)error,0xe8);
 redirect(0x5c4a3,0x300040,(uintptr_t)error,0xe9);
 vtable[0x130/8]=(uintptr_t)writeReg;at<void*>(access,0)=vtable;
 state.tglObservationController=controller;state.orgADLSPowerPG=(uintptr_t)pg;
 state.orgTGLPowerDDIState=(uintptr_t)(image+0x5c2ae);
 auto get=(void(*)(void*,unsigned))(image+0x5bfa8);
 auto put=(void(*)(void*,unsigned))(image+0x5aa86);
 // Reproduce the observed defect with the correction gated off: acquiring
 // TC1/TC2 works, releasing TC2 requests absent C while TC1 remains held.
 setup(12);state.adlsDisplaySelected=false;
 get(power,3);get(power,4);assert(requested==0x280 && !errors);
 put(power,4);assert(requested==0xA0 && delays==20 && errors==2);
 assert(XELPDonor::ddiReleaseSite(image,b.size()));
 assert(!XELPDonor::ddiReleaseSite(image,XELPDonor::DDIReleaseOffset+1));
 for(unsigned i=0;i<sizeof(XELPDonor::DDIReleaseBefore);++i) {
  image[XELPDonor::DDIReleaseOffset+i]^=1;assert(!XELPDonor::ddiReleaseSite(image,b.size()));image[XELPDonor::DDIReleaseOffset+i]^=1;
 }
 memcpy(image+XELPDonor::DDIReleaseOffset,XELPDonor::DDIReleaseAfter,sizeof(XELPDonor::DDIReleaseAfter));
 // TGL uses the same native producer with no ADL-S wrapper correction.
 // Real C refs must remain represented on layouts that provide C.
 for(unsigned mode:{0u,1u,2u,4u})for(bool realC:{false,true}) {
  setup(26);physicalMask=0x1FF;at<uint32_t>(ports[3],0x550)=mode;
  if(realC)get(power,2);
  get(power,3);get(power,4);put(power,4);
  assert(requested==((mode==2?0U:0x80U)|(realC?0x20U:0U)) && !delays && !errors);
  put(power,3);assert(requested==(realC?0x20U:0U));
  if(realC)put(power,2);
  assert(!requested && !delays && !errors);
 }
 physicalMask=0x79;state.adlsDisplaySelected=true;
 unsigned assignments=0,sequences=0,operations=0;
 for(unsigned assignment=1;assignment<27;++assignment){
  setup(assignment);std::vector<unsigned> acquire;
  for(unsigned d:{0u,3u,4u})if(state.adlsWiring.selected()&(1U<<d))acquire.push_back(d);
  ++assignments;
  do{auto release=acquire;std::sort(release.begin(),release.end());
   do{for(unsigned holds:{1u,3u}){
    setup(assignment);unsigned expectedRefs[9]{};
    auto check=[&](unsigned d,bool on){
     const auto before=at<uint32_t>(power,0x30+4*d);events.clear();
     if(on){get(power,d);++expectedRefs[d];}else{put(power,d);--expectedRefs[d];}
     for(unsigned i=0;i<9;++i)assert(at<uint32_t>(power,0x30+4*i)==expectedRefs[i]);
     assert(!errors && !delays && requested==expectedRequest());
     const bool transition=on?before==0:before==1;
     if(!transition)assert(events.empty());
     else {
      assert(events.size()==3);bool lane=requested&0x280;
      assert(events[lane?0:2].kind==0 && events[lane?0:2].value==(lane?0x2Au:0u));
      assert(events[lane?1:0].kind==0x45450 && events[lane?2:1].kind==0x45454);
      assert(events[lane?1:0].value==requested && events[lane?2:1].value==requested);
     }
     ++operations;
    };
    for(auto d:acquire)for(unsigned h=0;h<holds;++h)check(d,true);
    for(auto d:release)for(unsigned h=0;h<holds;++h)check(d,false);
    assert(!requested);++sequences;
   }}while(std::next_permutation(release.begin(),release.end()));
  }while(std::next_permutation(acquire.begin(),acquire.end()));
 }
 // The real native ACK loop must still detect a missing physical ACK.
 for(unsigned d:{0u,3u,4u}){setup(26);ackMissing=1U<<d;get(power,d);
  assert(requested==(2U<<(2*d)) && delays==20 && errors==2 && reads==20);
 }
 setup(26);get(power,3);get(power,4);ackMissing=1U<<3;reads=0;
 put(power,4);assert(requested==0x80 && delays==20 && errors==2 && reads==20);
 munmap(image,0x400000);
 printf("PASS exact donor DDI get/put and production wrapper: %u HDMI/DP/absent assignments, %u acquisition/release sequences, %u operations; baseline reproduced; 4 modeled physical-ACK faults retained. MMIO/parent hardware mocked.\n",assignments,sequences,operations);
}
