#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "kern_xelp_port_wait.hpp"
#include "kern_xelp_link_requirements.hpp"
#include "kern_xelp_sst_route.hpp"
#include "kern_tgl_tc_boot.hpp"
#include "kern_tgl_hdmi_wiring.hpp"
#include "kern_alderlake_s.hpp"
#include "kern_display13_fb_topology.hpp"
#include "kern_display13_fb_memory.hpp"
using mach_vm_address_t=uintptr_t;
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;} void setProperty(const char*,bool){}};
struct KernelPatcher {
 enum class Error{NoError,Failed};bool fail=false,corrupt=false;unsigned writes=0;
 struct LookupPatch {void *info;const uint8_t *before,*after;size_t n;unsigned count;};
 template<class T>T solveSymbol(size_t,const char*,uintptr_t a,size_t){return a+0x5f9f4;}
 void clearError(){} Error getError(){return fail?Error::Failed:Error::NoError;}
 void applyLookupPatch(LookupPatch *p,uint8_t *at,size_t){++writes;assert(!memcmp(at,p->before,p->n));if(!fail)memcpy(at,p->after,p->n);if(corrupt)at[0]^=1;}
};
struct XE {
 bool display13FramebufferRequested=false;XELP::Profile xeProfile;
 uint64_t tglPlatformCopy[0x1c0/sizeof(uint64_t)]{},adlnPlatform[0x1c0/sizeof(uint64_t)]{};
 Display13OutputWiring adlnOutputWiring;
 TGLHDMIWiring tglHDMIWiring;AlderLakeS::Wiring adlsWiring;
 TGLTCWiring tglTCWiring {TGLTCWiring::fromMasks(0,0,0)};
 bool adlsDisplaySelected=false,tglHDMIWiringApplied=false;
 uint32_t tglAUXParentMask=TGLTCWiring::Mask,tglTCAutoCandidates=0,tglTCAutoRegistered=0;
 uint32_t tglDPAltCandidates=0,tglComboDPMask=0;
 void *currentFramebuffer=nullptr;
 // ROUTING
 static XELP::LinkRequirements framebufferLinkRequirements(void*,uint32_t);
 static uint32_t framebufferPortWaitPolls(void*);
 bool processFramebufferPortWait(KernelPatcher&,size_t,uintptr_t,size_t);
} instance,*callbackXE=&instance;
// FUNCTIONS
alignas(8) uint8_t selectionPort[0x700]{};
void setup(unsigned platform,unsigned ddi,unsigned protocol){
 instance=XE{};instance.display13FramebufferRequested=platform==2;
 instance.adlsDisplaySelected=platform==1;
 instance.xeProfile.display=platform==2?XELP::Display::XeLPD:XELP::Display::XeD;
 auto &w=instance.adlnOutputWiring;w.valid=true;
 if(protocol==2){w.hdmi=1u<<ddi;w.ddc[ddi]=ddi<3?ddi+1:ddi+6;}
 else w.dp=1u<<ddi;
 auto bytes=reinterpret_cast<uint8_t*>(platform==2?instance.adlnPlatform:instance.tglPlatformCopy);
 bytes[0x12]=9;auto c=reinterpret_cast<uint32_t*>(bytes+0x20+24*ddi);
 c[0]=ddi;c[1]=ddi<3?ddi+1:ddi+6;c[2]=protocol;c[3]=platform==1 || ddi<3?0:1;c[4]=protocol==2?0x800:0x400;c[5]=protocol==2?0x202:0x201;
 if(platform==1) {
  assert(AlderLakeS::ComboBackendMask&(1u<<ddi));
  auto &a=instance.adlsWiring;a.valid=true;a.pin[ddi]=c[1];
  if(protocol==2)a.hdmi=1u<<ddi;else a.dp=1u<<ddi;
 } else if(platform==0) {
  if(protocol==2){auto &a=instance.tglHDMIWiring;a.valid=true;a.hdmi=1u<<ddi;a.ddc[ddi]=c[1];}
  else if(ddi<3)instance.tglComboDPMask=1u<<ddi;
  else instance.tglTCWiring=TGLTCWiring::fromMasks(1u<<ddi,0,0);
 }
 instance.tglHDMIWiringApplied=true;
 memset(selectionPort,0,sizeof(selectionPort));
 memcpy(selectionPort+0x548,&c,8);memcpy(selectionPort+0x120,&ddi,4);
 unsigned mode=platform==1 || ddi<3?1:4,reg=0x64000+256*ddi;
 memcpy(selectionPort+0x550,&mode,4);memcpy(selectionPort+0x590,&reg,4);
}
void checkInstallerAndSelection(){
 using namespace XELPPortWait;
 for(unsigned fault=0;fault<4;++fault){
  std::vector<uint8_t> b(0x70000);memcpy(b.data()+WaitOffset,WaitBefore,sizeof(WaitBefore));
  if(fault==1)b[WaitOffset+50]^=1;
  KernelPatcher p;p.fail=fault==2;p.corrupt=fault==3;
  const bool ok=instance.processFramebufferPortWait(p,0,reinterpret_cast<uintptr_t>(b.data()),b.size());
  assert(ok==!fault);assert(p.writes==(fault==1?0:1));
  if(ok){uintptr_t target=0;memcpy(&target,b.data()+WaitOffset+5,8);assert(target==reinterpret_cast<uintptr_t>(XE::framebufferPortWaitPolls));}
 }
 for(unsigned platform=0;platform<3;++platform)for(unsigned ddi:{0u,1u,3u,4u,5u,6u})for(unsigned protocol:{1u,2u}){
  if(platform==0 && ddi>=3 && protocol==2)continue;
  if(platform==1 && !(AlderLakeS::ComboBackendMask&(1u<<ddi)))continue;
  setup(platform,ddi,protocol);
  assert(XE::framebufferPortWaitPolls(selectionPort)==(platform==1 || ddi<3?200:600));
  using P=XELP::LinkRequirements::Preparation;
  const auto req=XE::framebufferLinkRequirements(selectionPort,platform==1 || ddi<3?1:4);
  assert(req.preparation(false)==(platform==1 || ddi<3?P::NativeCombo:platform==0?P::NativeTC12:P::ColdTC13));
  assert(req.preparation(true)==(platform==1 || ddi<3?P::NativeCombo:platform==0?P::NativeTC12:P::FirmwareTC13));
  auto config=Display13FBMemory::field<uint32_t *>(selectionPort,0x548);
  uint32_t clone[6];memcpy(clone,config,sizeof(clone));auto clonePtr=clone;
  memcpy(selectionPort+0x548,&clonePtr,sizeof(clonePtr));assert(XE::framebufferPortWaitPolls(selectionPort)==100);
  memcpy(selectionPort+0x548,&config,sizeof(config));
  if(platform!=2 && protocol==2){
   instance.tglHDMIWiringApplied=false;assert(XE::framebufferPortWaitPolls(selectionPort)==100);
   instance.tglHDMIWiringApplied=true;config[1]^=1;assert(XE::framebufferPortWaitPolls(selectionPort)==100);config[1]^=1;
  }
  if(platform==1 && protocol==1){
   instance.adlsWiring.valid=false;assert(XE::framebufferPortWaitPolls(selectionPort)==100);instance.adlsWiring.valid=true;
  }
  if(platform==0 && ddi>=3){
   instance.tglTCWiring=TGLTCWiring::fromMasks(0,0,0);assert(XE::framebufferPortWaitPolls(selectionPort)==100);
   instance.tglTCAutoCandidates=1u<<ddi;assert(XE::framebufferPortWaitPolls(selectionPort)==100);
   instance.tglTCAutoRegistered=1u<<ddi;assert(XE::framebufferPortWaitPolls(selectionPort)==600);
   instance.tglAUXParentMask=0;assert(XE::framebufferPortWaitPolls(selectionPort)==100);
   instance.tglAUXParentMask=TGLTCWiring::Mask;
   instance.tglComboDPMask=1u<<ddi;assert(XE::framebufferPortWaitPolls(selectionPort)==100);instance.tglComboDPMask=0;
  }
  selectionPort[0x117]=1;assert(XE::framebufferPortWaitPolls(selectionPort)==100);selectionPort[0x117]=0;
  selectionPort[0x590]^=1;assert(XE::framebufferPortWaitPolls(selectionPort)==100);selectionPort[0x590]^=1;
  selectionPort[0x120]^=1;assert(XE::framebufferPortWaitPolls(selectionPort)==100);selectionPort[0x120]^=1;
  unsigned mode=3;memcpy(selectionPort+0x550,&mode,4);assert(XE::framebufferPortWaitPolls(selectionPort)==100);
 }
 puts("PASS real common installer guard/failure/readback and production wiring-to-PHY/protocol selector, including ADL-S TC-numbered Combo, panel/alt/mismatched native exclusion");
}
