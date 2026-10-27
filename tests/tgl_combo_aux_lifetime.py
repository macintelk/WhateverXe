#!/usr/bin/env python3
"""Execute the production main-link AUX lifetime against native refcount mocks."""
from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'WhateverXe/kern_xe.cpp').read_text()
a=s.index('void XE::tglComboAUXReference(');fn=s[a:s.index('uint32_t XE::wrapTGLDisablePort',a)].replace('XE::tglComboAUXReference','run')
a=s.index('uint32_t XE::wrapTGLEnablePort(');enable=s[a:s.index('// Exact 20A2314a table.',a)].replace('XE::wrapTGLEnablePort','enable').replace('tglComboAUXReference','run')
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes();assert b[0x5fc56:0x5fc5f]==bytes.fromhex('554889e553504889fb');assert b[0x5fc6d:0x5fc73]==bytes.fromhex('8bb790050000')
code=r'''
#include "kern_alderlake_s.hpp"
#include "kern_tgl_combo_dp.hpp"
#include "kern_xelp_sst_route.hpp"
#include <cassert>
#include <map>
#include <cstdio>
struct Functions {bool ready=true;void **controllerSlot=nullptr;void(*getAux)(void*)=nullptr;void(*putAux)(void*)=nullptr;bool complete(){return ready;}};
struct State {uint32_t (*orgTGLEnablePort)(void*)=nullptr;uint32_t tglComboDPMask=0;bool adlsDisplaySelected=false;AlderLakeS::Wiring adlsWiring;alignas(8) uint8_t tglPlatformCopy[0x1c0]{};void *tglObservationController=nullptr;bool tglActive=true,tglComboAUXLifetimeReady=true;
 struct {XELP::Family family=XELP::Family::TigerLake;} xeProfile;
 XELP::VBT::Ports firmwarePorts;Functions tglTCNativeFunctions;void *tglComboAUXOwners[9]{};
} state,*callbackXE=&state;
std::map<unsigned,unsigned> regs;unsigned getCount,putCount,reads,nativeEnableCount;bool missingAck;unsigned elapsed,ackDelay;bool dropRequest;unsigned badForGet;uint32_t *refs;
uint32_t wrapTGLRegisterRead(void *,unsigned long reg){++reads;return regs[reg];}
void IODelay(unsigned us){elapsed+=us;if(dropRequest)regs[0x45444]=0;else if(ackDelay && elapsed>=ackDelay)regs[0x45444]=regs[0x45454];}
void tglTransitionRecord(unsigned stage,bool,void *,unsigned,unsigned,unsigned,const void *,unsigned){assert(stage==74);}
void tglComboAUXDecisionRecord(uint32_t *){}
FUNCTION
#define FunctionCast(name,target) (target)
constexpr uint32_t kIOReturnNotReady=0xe00002d8U;
template<class... A> void tglTrace(A...){}
void tglSnapshotTransmitter(void*,bool,unsigned){}
ENABLE
int main(){
 for(unsigned testDDI:{0u,1u,3u,4u})for(unsigned kind=0;kind<4;++kind)for(unsigned bad=0;bad<18;++bad)for(unsigned initialRefs:{0u,2u}){
 const bool adls=kind==1||kind==3,fixed=kind==2;const bool panel=kind==3;if(panel && testDDI!=0)continue;if(adls?testDDI==1:testDDI>1)continue;
 alignas(8) uint8_t p[0x700]{},ctl[0xD00]{},power[0x100]{},access[0x100]{};uint32_t local[6]{};uint32_t *config=local;
 unsigned ddi=testDDI,mask=3U<<(2*ddi),buffer=0x64000+256*ddi;
 auto store=[](void *b,unsigned o,auto v){memcpy(static_cast<uint8_t *>(b)+o,&v,sizeof(v));};
 state={};nativeEnableCount=0;state.orgTGLEnablePort=[](void*){++nativeEnableCount;return 0U;};if(adls) {
  state.adlsDisplaySelected=true;state.adlsWiring.valid=true;state.adlsWiring.dp=1U<<ddi;state.adlsWiring.pin[ddi]=ddi?ddi+6:1;state.xeProfile.family=XELP::Family::AlderLakeS;
  config=reinterpret_cast<uint32_t *>(state.tglPlatformCopy+0x20+24*ddi);const uint32_t c[]={ddi,ddi?ddi+6:1,1,0,0x400,0x201};memcpy(config,c,sizeof(c));
 }
 if(panel){state.adlsWiring.dp=0;state.adlsWiring.panel=1;const uint32_t c[]={0,0,0,0,2,0x18};memcpy(config,c,sizeof(c));}
 if(fixed) {
  state.tglComboDPMask=1U<<ddi;config=reinterpret_cast<uint32_t *>(state.tglPlatformCopy+0x20+24*ddi);
  const uint32_t c[]={ddi,ddi+1,1,0,0x400,0x201};memcpy(config,c,sizeof(c));
 } else if(!adls)config[0]=ddi;
 state.tglObservationController=ctl;void *global=ctl;
 state.tglTCNativeFunctions.controllerSlot=&global;
 state.firmwarePorts.status=XELP::VBT::Status::Valid;state.firmwarePorts.present=1U<<ddi;state.firmwarePorts.port[ddi].type=(adls&&!panel||fixed)?0x60d6:0x1806;
 store(p,0x548,config+0);store(p,0x550,1U);store(p,0x578,access+0);store(p,0x590,buffer);
 store(ctl,0xC8,power+0);store(ctl,0xC40,access+0);store(power,0x10,ctl+0);store(power,0x78,access+0);
 store(access,0x60,0x200000U);store(access,0x50,ctl+0);
 badForGet=bad;refs=reinterpret_cast<uint32_t *>(power+0x54+4*ddi);*refs=initialRefs;getCount=putCount=reads=0;missingAck=bad>=14;elapsed=0;ackDelay=bad==15?500:0;dropRequest=bad==16;
 regs.clear();regs[0x45454]=mask;regs[buffer]=0x81;
 state.tglTCNativeFunctions.getAux=[](void*){++*refs;++getCount;regs[0x45444]=missingAck?(badForGet==17?~0U:regs[0x45454]&0xAAAAAAAAU):regs[0x45454];};
 state.tglTCNativeFunctions.putAux=[](void*){--*refs;++putCount;};
 if(bad==1)state.tglActive=false;
 if(bad==2)state.tglComboAUXLifetimeReady=false;
 if(bad==3)state.tglTCNativeFunctions.ready=false;
 if(bad==4)global=nullptr;
 if(bad==5)config[0]=2;
 if(bad==6)config[2]=2;
 if(bad==7)state.firmwarePorts.present=0;
 if(bad==8)store(power,0x78,ctl+0);
 if(bad==9)store(p,0x590,buffer+0x100);
 if(bad==10)regs[0x45454]=0;
 if(bad==11)regs[buffer]=0x80000001;
 if(bad==12){state.xeProfile.family=XELP::Family::AlderLakeN;state.adlsDisplaySelected=false;}
 if(bad==13)*refs=~0U;
 auto before=*refs;run(p,true);
 if(bad && bad!=15){assert(*refs==before&&!state.tglComboAUXOwners[ddi]);assert(getCount==(bad>=14));assert(putCount==(bad>=14));
 if(bad>=14) {
  assert(elapsed==(bad==14?1000:bad==16?10:0));
  if(adls||fixed){assert(enable(p)==kIOReturnNotReady);assert(nativeEnableCount==0);}
 }
 continue;}
 assert(getCount==1&&*refs==initialRefs+1&&state.tglComboAUXOwners[ddi]==p);assert(elapsed==(bad==15?500:0));
 assert(enable(p)==0 && nativeEnableCount==1);run(p,true);assert(getCount==1); // duplicate enable is idempotent
 // Native per-transaction ref pairs cannot consume the held main-link ref.
 ++*refs;--*refs;assert(*refs==initialRefs+1);
 run(nullptr,false);assert(putCount==0);
 regs[buffer]=0x80000001;run(p,false);assert(putCount==0); // active main link
 regs[buffer]=0x81;run(p,false);assert(putCount==1&&*refs==initialRefs&&!state.tglComboAUXOwners[ddi]);
 run(p,false);assert(putCount==1);run(p,true);assert(getCount==2);run(p,false);assert(putCount==2&&*refs==initialRefs);
 }
 puts("PASS main-link AUX acquire/duplicate/native transaction/disable/retry, inherited refs, ownership/power/ACK guards");
}
'''.replace('FUNCTION',fn).replace('\nENABLE\n','\n'+enable+'\n')
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
