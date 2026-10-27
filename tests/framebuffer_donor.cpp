#include "kern_framebuffer_donor.hpp"
#include <cassert>
#include <array>
#include <map>
#include <vector>
#include <cstdio>
struct Graph {
 std::vector<uint8_t> fb=std::vector<uint8_t>(0x4b90),controller=std::vector<uint8_t>(0x1c20),manager=std::vector<uint8_t>(0x68);
 std::array<std::vector<uint8_t>,15> planes;
 std::array<std::vector<uint8_t>,6> scalers;
 std::map<void*,size_t> sizes;
 size_t size(void *p){auto i=sizes.find(p);return i==sizes.end()?0:i->second;}
 template<class T>void put(void *p,size_t offset,T value){memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));}
 Graph(){
  sizes[fb.data()]=fb.size();sizes[controller.data()]=controller.size();sizes[manager.data()]=manager.size();
  put(fb.data(),0x1d0,controller.data());put(fb.data(),0x1dc,1U);put(controller.data(),0xc40,manager.data());
  put(manager.data(),0x50,uintptr_t(0x100000));put(manager.data(),0x60,0x1000000U);
  for(unsigned i=0;i<15;++i){planes[i].resize(0x1c8);auto p=planes[i].data();sizes[p]=planes[i].size();put(p,0x7c,i);put(controller.data(),0xf58+i*8,p);}
  for(unsigned i=0;i<6;++i){scalers[i].resize(0x58);auto p=scalers[i].data();sizes[p]=scalers[i].size();put(p,0x1c,i);put(controller.data(),0xf28+i*8,p);}
 }
 bool bind(){return FramebufferDonor::bindManagers(*this,fb.data(),controller.data(),1);}
};
int main(){
 for(unsigned mapBytes:{0x200000U,0x1000000U}){
  Graph g;g.put(g.manager.data(),0x60,mapBytes);assert(g.bind()&&g.bind());
  for(auto &p:g.planes)assert(FramebufferDonor::field<void*>(p.data(),0x90)==g.manager.data());
  for(auto &p:g.scalers)assert(FramebufferDonor::field<void*>(p.data(),0x28)==g.manager.data());
  assert(FramebufferDonor::field<void*>(g.fb.data(),0x4a40)==g.manager.data());
 }
 for(unsigned failure=0;failure<7;++failure){
  Graph g;
  if(failure==0)g.put(g.fb.data(),0x1d0,static_cast<void*>(nullptr));
  if(failure==1)g.put(g.fb.data(),0x1dc,2U);
  if(failure==2)g.put(g.manager.data(),0x60,0x60404U);
  if(failure==3)g.put(g.manager.data(),0x50,uintptr_t(0));
  if(failure==4)g.put(g.controller.data(),0xc40,static_cast<void*>(nullptr));
  if(failure==5)g.sizes[g.controller.data()]=0x100;
  if(failure==6)g.sizes[g.fb.data()]=0x100;
  auto fb=g.fb;auto planes=g.planes;auto scalers=g.scalers;assert(!g.bind());
  assert(g.fb==fb&&g.planes==planes&&g.scalers==scalers);
 }
 // A failure at the last borrower must not mutate any earlier empty slot.
 for(unsigned index=0;index<22;++index)for(unsigned kind=0;kind<3;++kind){
  Graph g;void *p=index<15?g.planes[index].data():index<21?g.scalers[index-15].data():g.fb.data();
  auto slot=index<15?0x90:index<21?0x28:0x4a40;
  if(kind==0)g.put(p,slot,reinterpret_cast<void*>(0xdead));
  if(kind==1)g.sizes[p]=1;
  if(kind==2)g.put(p,index<15?0x7c:index<21?0x1c:0x1dc,99U);
  auto fb=g.fb;auto planes=g.planes;auto scalers=g.scalers;assert(!g.bind());
  assert(g.fb==fb&&g.planes==planes&&g.scalers==scalers);
 }
 puts("PASS common FB/plane/scaler binding: all-object validation, conflicting borrowers, no partial commit");
}
