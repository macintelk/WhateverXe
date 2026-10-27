#!/usr/bin/env python3
"""Exercise the production pre-start property selection with IOKit mocks."""
from pathlib import Path
import subprocess,tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'WhateverXe/kern_xe.cpp').read_text()
start=source.index('bool XE::wrapDisplay13FramebufferStart(')
end=source.index('const auto started=FunctionCast(',start)
block=source[source.index('\tauto old=OSDynamicCast(OSDictionary,',start):end]
code=r'''
#include <cassert>
#define SYSLOG(...) ((void)0)
#include <cstdint>
#include <cstring>
#include "kern_display13_fb_topology.hpp"
Display13OutputWiring board;
#include <map>
#include <memory>
#include <string>
#include <vector>
unsigned step=0,failAt=0;
bool allowed(){return ++step!=failAt;}
struct OSObject {virtual ~OSObject()=default;};
std::vector<std::unique_ptr<OSObject>> objects;
template<class T> T *make(){if(!allowed())return nullptr;auto p=new T;objects.emplace_back(p);return p;}
struct OSNumber:OSObject {
 uint64_t value=0;
 static OSNumber *withNumber(uint64_t v,unsigned bits){assert(bits==32);auto p=make<OSNumber>();if(p)p->value=v;return p;}
};
struct OSData:OSObject {uint32_t value=0;};
struct OSDictionary:OSObject {
 std::map<std::string,OSObject *> items;
 static OSDictionary *withDictionary(OSDictionary *old){auto p=make<OSDictionary>();if(p)p->items=old->items;return p;}
 static OSDictionary *withCapacity(unsigned){return make<OSDictionary>();}
 bool setObject(const char *key,OSObject *v){if(!allowed())return false;items[key]=v;return true;}
};
struct IOService {
 std::map<std::string,OSObject *> items;bool workLoop=true;
 void *getWorkLoop(){return workLoop?this:nullptr;}
 OSObject *getProperty(const char *key){auto i=items.find(key);return i==items.end()?nullptr:i->second;}
 bool setProperty(const char *key,OSObject *v){if(!allowed())return false;items[key]=v;return true;}
 bool setProperty(const char *key,void *v,unsigned size){
  assert(size==4);auto p=make<OSData>();if(!p)return false;memcpy(&p->value,v,size);return setProperty(key,p);
 }
};
#define OSDynamicCast(T,p) dynamic_cast<T *>(p)
#define OSSafeReleaseNULL(p) do{p=nullptr;}while(0)
struct Handoff {
 bool adlpDisplaySelected=true,ready=true;unsigned calls=0;
 bool prepareDisplay13BootScanout(IOService *provider){assert(provider);++calls;return ready;}
} handoff;
bool prepare(IOService *that,IOService *provider){
 auto s=&handoff;auto pci=provider;
PRODUCTION
 return true;
}
void check(bool oldFeatures,bool oldKey,uint32_t keyValue,unsigned failure,bool workLoop=true){
 objects.clear();step=failAt=0;IOService that,provider;provider.workLoop=workLoop;
 OSObject untouched;OSData key;key.value=keyValue;OSDictionary prior;OSNumber disabled;disabled.value=0;
 prior.items={{"UnrelatedFeature",&untouched},{"YTiling",&disabled}};
 if(oldFeatures)that.items["FeatureControl"]=&prior;
 if(oldKey)provider.items["AAPL,GfxYTile"]=&key;
 step=0;failAt=failure;handoff.calls=0;bool ok=prepare(&that,&provider);
 if(failure || !workLoop){assert(!ok && !handoff.calls);return;}
 assert(handoff.calls==unsigned(handoff.adlpDisplaySelected || (board.hdmi&0x78)));
 assert(ok);auto f=dynamic_cast<OSDictionary *>(that.getProperty("FeatureControl"));assert(f);
 assert(dynamic_cast<OSNumber *>(f->items.at("YTiling"))->value==1);
 assert(dynamic_cast<OSNumber *>(f->items.at("ThreadingMode"))->value==0);
 if(oldFeatures){assert(f!=&prior && f->items.at("UnrelatedFeature")==&untouched);assert(prior.items.at("YTiling")==&disabled);}
 auto data=dynamic_cast<OSData *>(provider.getProperty("AAPL,GfxYTile"));assert(data);
 assert(oldKey ? data==&key && data->value==keyValue : data->value==1);
}
int main(){
 {IOService that,provider;handoff.ready=false;assert(!prepare(&that,&provider));assert(handoff.calls==1);handoff.ready=true;}

 for(bool mobile:{false,true})for(unsigned mask:{2U,8U})
 for(bool features:{false,true})for(bool key:{false,true})for(uint32_t value:{0U,1U}){
  handoff.adlpDisplaySelected=mobile;board.hdmi=mask;
  check(features,key,value,0);unsigned operations=step;
  for(unsigned failure=1;failure<=operations;++failure)check(features,key,value,failure);
  check(features,key,value,0,false);
 }
}
'''.replace('PRODUCTION',block)
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(code)
    subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS: pre-start Y selection, existing-property preservation and setup failure propagation')
