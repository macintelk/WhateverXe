#!/usr/bin/env python3
"""Shared production lease/adoption with exact donor AGDC and native path lookup.

Power/PHY/sink boundaries are mocks. This detects integration and ownership
regressions; it does not establish hardware readiness or requirement coverage.
"""
from pathlib import Path
import hashlib,re,struct,subprocess,sys,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text();h=(r/'WhateverXe/kern_xe.hpp').read_text()
fields=h[h.index('\tstruct TGLTrainingLease {'):h.index('\tmach_vm_address_t orgTGLGetOSInformation')]
body=s[s.index('XE::TGLTrainingLease *XE::tglTrainingLeaseForDDI'):s.index('void XE::tglObserveActivation(')]
adopt=s[s.index('bool XE::wrapTGLAllocatePathResources'):s.index('void *XE::wrapTGLAddPath')]
f=(r/'tests/tgl_training_leases_fixture.cpp').read_text().split('int main(')[0]
f=f.replace('// TC_WIRING_FIELDS',h[h.index('\tbool tglAUXParentReady'):h.index('\tstatic void wrapTGLEnableAUXPower')])
f=f.replace('// LEASE_FIELDS',fields).replace('// LEASE_FUNCTIONS',body+adopt)
b=Path(sys.argv[1]).read_bytes();assert hashlib.sha256(b).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
# Symbols resolve pool globals and native boundary entries from the accepted donor.
pos=32;symbols={}
for _ in range(struct.unpack_from('<I',b,16)[0]):
 cmd,size=struct.unpack_from('<II',b,pos)
 if cmd==2:
  symoff,nsyms,stroff,strsize=struct.unpack_from('<IIII',b,pos+8)
  for i in range(nsyms):
   name,typ,sect,desc,value=struct.unpack_from('<IBBHQ',b,symoff+16*i)
   end=b.index(b'\0',stroff+name);symbols[b[stroff+name:end].decode()]=value
 pos+=size
# Entries and side-effect spans are also checked independently of route installation.
contract=(r/'WhateverXe/kern_framebuffer_training_native.hpp').read_text()
for name,off,values in re.findall(r'\{"([^"]+)",0x([0-9a-f]+),\{([^}]+)\}\}',contract):
 off=int(off,16);v=bytes(int(x,0) for x in values.split(','));assert symbols[name]==off and b[off:off+len(v)]==v
for off,length,values in re.findall(r'\{0x([0-9a-f]+),(\d+),\{([^}]+)\}\}',contract):
 off=int(off,16);v=bytes(int(x,0) for x in values.split(','));assert len(v)==int(length) and b[off:off+len(v)]==v
extra=r'''
#include <fstream>
#include <iterator>
#include <vector>
uint8_t *image;void *nativePaths[4];unsigned linkCalls,clockStarts,clockStops;bool rejectPowerGet,rejectPowerPut,stoppedProof;
bool clockProof(void*,void*){return stoppedProof;}
void jump(unsigned off,uintptr_t target){image[off]=0x48;image[off+1]=0xb8;memcpy(image+off+2,&target,8);image[off+10]=0xff;image[off+11]=0xe0;}
void *builtIn(){return nullptr;}bool hpd(void*){return false;}
uint32_t dpcd(void*){return 0;}
void setLink(void *p,void*){assert(p);}
void setupClock(void*,void*){}
void enableClock(void*,void*,void*,void*){++clockStarts;}
void stopClock(void*c,void*p,void*o){++clockStops;disableClocks(c,p,o);}
void *poolAllocate(){auto p=reinterpret_cast<void*(*)()>(image+0x9188a)();
 if(p)for(unsigned i=0;i<3;++i)if(p==paths[i].data())used[i]=true;return p;}
void *poolLookup(void*p){return reinterpret_cast<void*(*)(void*)>(image+0x91bca)(p);}
uint32_t sinkTraining(void*p,void*out){
 ++linkCalls;auto path=poolLookup(p);assert(path); // r23 failed here, before sink/PHY training.
 auto &lease=obj.tglTrainingLeases[reinterpret_cast<uint8_t*>(p)==ports[3].data()?3:
  reinterpret_cast<uint8_t*>(p)==ports[4].data()?4:reinterpret_cast<uint8_t*>(p)==ports[5].data()?5:6];
 assert(path==lease.path && lease.powerOwned && lease.trainingThread==current_thread());
 assert(pgRefs[lease.pipe?lease.pipe+2:1]==1);
 assert(!*reinterpret_cast<void**>(static_cast<uint8_t*>(path)+0x32c8)); // No primary plane yet.
 static_cast<uint32_t*>(out)[0x38/4]=0;return 0;
}
bool adapterValidate(void*c,void*p,uint32_t pipe){
 if(c!=ctl.data() || !p)return false;
 return pipe==UINT32_MAX || (pipe<3 && pgRefs[pipe?pipe+2:1]>0);
}
bool adapterPower(void*p,uint32_t pg,bool get){
 if((get&&rejectPowerGet)||(!get&&rejectPowerPut))return false;
 if(get)enablePG(p,pg);else disablePG(p,pg);return true;
}
void init(unsigned ddi,unsigned pipe){
 setup();rejectPowerGet=rejectPowerPut=false;linkCalls=clockStarts=clockStops=0;
 obj.trainingLeaseDP13Mask=0x78;obj.trainingLeaseValidate=adapterValidate;obj.trainingLeasePower=adapterPower;
 obj.orgTGLPathLookup=(uintptr_t)poolLookup;obj.orgTGLAllocatePathAny=(uintptr_t)poolAllocate;
 obj.orgTGLTrainingBegin=(uintptr_t)(image+0x6aae0);obj.orgTGLTraining=(uintptr_t)(image+0x6ab7e);obj.orgTGLTrainingEnd=(uintptr_t)(image+0x6ad06);
 for(unsigned i=0;i<3;++i){nativePaths[i]=paths[i].data();paths[i].at<uint32_t>(0x3644)=i==pipe?0:2;}
 // Other native pipes are reserved; free pipe is selected by the actual allocator.
 static uint32_t config[6];config[0]=ddi;config[2]=1;ports[ddi].at<void*>(0x548)=config;ports[ddi].at<uint8_t>(0x11)=4;
 static uintptr_t vt[64];vt[0x130/8]=(uintptr_t)disablePort;ports[ddi].at<void*>(0)=vt;
}
int main(int argc,char**argv){
 std::ifstream input(argv[1],std::ios::binary);std::vector<char> b((std::istreambuf_iterator<char>(input)),{});
 image=(uint8_t*)mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0);assert(image!=MAP_FAILED);memcpy(image,b.data(),b.size());
 INSTALL
 assert(image[0x6aba6]==0xe8);jump(0x300000,(uintptr_t)memcpy);
 int32_t copyCall=0x300000-0x6aba6-5;memcpy(image+0x6aba7,&copyCall,4);
 *reinterpret_cast<void***>(image+PATHS)=nativePaths;*reinterpret_cast<uint32_t*>(image+COUNT)=3;
 for(unsigned ddi=3;ddi<7;++ddi)for(unsigned pipe=0;pipe<3;++pipe){
  init(ddi,pipe);uint32_t req[20]={ddi+1},out[20]={};req[4]=4U<<24;
  assert(!XE::wrapTGLTrainingBegin(ctl.data(),req));assert(!poolLookup(ports[ddi].data()));
  assert(!XE::wrapTGLTraining(ctl.data(),req,out));auto &lease=obj.tglTrainingLeases[ddi];auto path=lease.path;
  assert(path==paths[pipe].data()&&linkCalls==1&&clockStarts==1);
  assert(!XE::wrapTGLTraining(ctl.data(),req,out));assert(lease.path==path&&clockStops==1&&clockStarts==2);
  req[2]=1;assert(!XE::wrapTGLTrainingEnd(ctl.data(),req));assert(lease.path==path);
  // Native resource allocation acquires its own PG before returning. The
  // production wrapper must return only the reservation's extra reference.
  nativeBoundary=[&]{enablePG(power.data(),lease.pg);};
  assert(XE::wrapTGLAllocatePathResources(fbs[0].data(),ddi,0));nativeBoundary={};
  assert(!lease.path&&pgRefs[pipe?pipe+2:1]==1);disablePG(power.data(),pipe?pipe+2:1);freePath(path);clean();
  // Abort End consumes native stop exactly once and releases the reservation.
  init(ddi,pipe);req[2]=0;assert(!XE::wrapTGLTraining(ctl.data(),req,out));
  assert(!XE::wrapTGLTrainingEnd(ctl.data(),req));assert(!obj.tglTrainingLeases[ddi].path&&clockStops==1);clean();
 }
 // A void native clock-stop return is not a completed cleanup.
 init(5,1);uint32_t endReq[20]={6},endOut[20]={};
 assert(!XE::wrapTGLTraining(ctl.data(),endReq,endOut));obj.trainingLeaseStop=clockProof;stoppedProof=false;
 assert(XE::wrapTGLTrainingEnd(ctl.data(),endReq)==kIOReturnBusy&&clockStops==1);
 assert(obj.tglTrainingLeases[5].path&&obj.tglTrainingLeases[5].powerOwned);
 stoppedProof=true;assert(!XE::wrapTGLTrainingEnd(ctl.data(),endReq)&&clockStops==1);clean();
 // An in-flight call blocks native teardown. The refusal is bounded and the
 // controller stays retained until a later real stop can drain the pool.
 init(5,1);assert(!XE::wrapTGLTraining(ctl.data(),endReq,endOut));
 obj.tglTrainingLeases[5].busy=true;XE::wrapTGLControllerStop(ctl.data(),nullptr);
 assert(!stopCalled&&obj.tglStopRetained&&retained[ctl.data()]==1);
 obj.tglTrainingLeases[5].busy=false;XE::wrapTGLControllerStop(ctl.data(),nullptr);
 assert(stopCalled&&!obj.tglStopRetained);clean();
 // Adoption owns the native path even when our extra PG put fails. Stop
 // retries only that extra reference and must not inspect the adopted path.
 init(5,1);assert(!XE::wrapTGLTraining(ctl.data(),endReq,endOut));rejectPowerPut=true;
 nativeBoundary=[&]{enablePG(power.data(),obj.tglTrainingLeases[5].pg);};
 assert(!XE::wrapTGLAllocatePathResources(fbs[0].data(),5,0));nativeBoundary={};
 auto &adopted=obj.tglTrainingLeases[5];auto nativePath=adopted.path;auto nativePG=adopted.pg;
 assert(adopted.adopted&&adopted.powerOwned&&pgRefs[nativePG]==2);
 adopted.path=(void*)1; // Native-owned path may already be destroyed.
 XE::wrapTGLControllerStop(ctl.data(),nullptr);assert(!stopCalled&&obj.tglStopRetained&&adopted.adopted);
 rejectPowerPut=false;XE::wrapTGLControllerStop(ctl.data(),nullptr);
 assert(stopCalled&&!adopted.path&&pgRefs[nativePG]==1);
 disablePG(power.data(),nativePG);freePath(nativePath);clean();
 // Failed power acquisition retains the reservation and cannot train on retry.
 init(5,1);uint32_t req[20]={6},out[20]={};rejectPowerGet=true;
 assert(XE::wrapTGLTraining(ctl.data(),req,out)==kIOReturnBusy&&linkCalls==0);
 assert(XE::wrapTGLTraining(ctl.data(),req,out)==kIOReturnBusy&&linkCalls==0);
 XE::wrapTGLControllerStop(ctl.data(),nullptr);
 assert(!stopCalled&&obj.tglStopRetained&&obj.tglTrainingLeases[5].path&&!obj.tglTrainingLeases[5].powerOwned);
 assert(retained[ctl.data()]==1); // Unproven transition remains quarantined, not synthetic cleanup.
 munmap(image,0x400000);
 puts("PASS production Display13 lease dispatch with native AGDC Begin/Train/retry/End and pool lookup; mock power/sink/adoption boundaries");
}
'''
install=[]
for symbol,fn in [('__ZN14AppleIntelPort14getPortByIndexEj','getPort'),('__ZN14AppleIntelPort14getBuiltInPortEv','builtIn'),('__ZN14AppleIntelPort8isHPDLowEv','hpd'),('__ZN14AppleIntelPort11getDPCDInfoEv','dpcd'),('__ZN14AppleIntelPort13setLinkConfigEP18AGDCDPLinkConfig_t','setLink'),('__ZN31AppleIntelFramebufferController11setupClocksEP14AppleIntelPort','setupClock'),('__ZN31AppleIntelFramebufferController12enableClocksEP14AppleIntelPortS1_P11ClockParams','enableClock'),('__ZN31AppleIntelFramebufferController13disableClocksEP14AppleIntelPortS1_','stopClock'),('__ZN14AppleIntelPort12linkTrainingEP18AGDCDPPortConfig_t','sinkTraining')]:
 install.append(f'jump(0x{symbols[symbol]:x},(uintptr_t){fn});')
extra=extra.replace('INSTALL','\n '.join(install)).replace('PATHS',str(symbols['__ZN21AppleIntelDisplayPath6fPathsE'])).replace('COUNT',str(symbols['__ZN21AppleIntelDisplayPath10fPathCountE']))
with tempfile.TemporaryDirectory(prefix='display13-training-') as d:
 p=Path(d);(p/'check.cpp').write_text(f+extra)
 subprocess.run(['clang++','-arch','x86_64','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'check.cpp'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check'),sys.argv[1]],check=True)
