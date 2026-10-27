#include "kern_vbt.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
#include <iterator>
using namespace XELP;
using namespace XELP::VBT;
void w16(std::vector<uint8_t>&b,size_t at,uint16_t v){b[at]=v;b[at+1]=v>>8;}
void w32(std::vector<uint8_t>&b,size_t at,uint32_t v){w16(b,at,v);w16(b,at+2,v>>16);}
void checksum(std::vector<uint8_t>&b){b[26]=0;uint8_t sum=0;for(auto v:b)sum+=v;b[26]=-sum;}
std::vector<uint8_t> table(const std::vector<Port>&ports){
 std::vector<uint8_t>b(48+22+3+5+39*ports.size());memcpy(b.data(),"$VBT",4);
 w16(b,20,100);w16(b,22,48);w16(b,24,b.size());w32(b,28,48);
 memcpy(b.data()+48,"BIOS_DATA_BLOCK ",16);w16(b,64,250);w16(b,66,22);w16(b,68,b.size()-48);
 b[70]=2;w16(b,71,b.size()-73);b[77]=39;
 for(size_t i=0;i<ports.size();++i){auto at=78+39*i;const auto &p=ports[i];w16(b,at+2,p.type);
  b[at+16]=p.dvo;b[at+19]=p.ddc;b[at+25]=p.aux;b[at+23]=p.flags;b[at+33]=p.capabilities;}
 checksum(b);return b;
}
int main(int argc,char **argv){
 if(argc==3){std::ifstream f(argv[2],std::ios::binary);assert(f);std::vector<uint8_t>b{std::istreambuf_iterator<char>(f),{}};
  const auto family=!strcmp(argv[1],"adls")?Family::AlderLakeS:!strcmp(argv[1],"12")?Family::TigerLake:Family::AlderLakeN;
  auto r=parse(b.data(),b.size(),family);auto w=r.hdmi(3);
  printf("status=%u bdb=%u present=%x unmapped=%u ambiguous=%x hdmi=%x pins=%u,%u,%u valid=%u\n",unsigned(r.status),r.version,r.present,r.unmapped,r.ambiguous,w.hdmi,w.ddc[0],w.ddc[1],w.ddc[2],w.valid);
  for(unsigned d=0;d<9;++d)if(r.present&(1U<<d)){auto q=r.port[d];printf("ddi=%u type=%x dvo=%u ddc=%u aux=%x flags=%x caps=%x\n",d,q.type,q.dvo,q.ddc,q.aux,q.flags,q.capabilities);}
  printf("panel valid=%u type=%u ddi=%u low=%u hobl=%u\n",r.panel.valid,r.panel.type,r.panel.ddi,r.panel.lowSwing,r.panel.hobl);
  return !r.valid();
 }
 // DP-alt firmware admission is independent of fixed-DP policy and mode.
 for(unsigned d=3;d<9;++d) {
  Ports q;q.status=Status::Valid;q.present=1U<<d;
  q.port[d]={0x68c6,9,0,d==3?0x30U:(d+1)*0x10U,0xc0,1};
  assert(q.tglDPAltCandidates(0x1f8)==(1U<<d));
  assert(q.tglDPAltCandidates(0x18)==(d<5?1U<<d:0));
  auto bad=q;bad.port[d].capabilities=2;assert(!bad.tglDPAltCandidates(0x1f8));
  bad=q;bad.port[d].aux=0x40;assert(!bad.tglDPAltCandidates(0x1f8));
  bad=q;bad.port[d].type|=0x1000;assert(!bad.tglDPAltCandidates(0x1f8));
  bad=q;bad.port[d].flags|=4;assert(!bad.tglDPAltCandidates(0x1f8));
  bad=q;bad.present|=1;bad.port[0]=q.port[d];assert(!bad.tglDPAltCandidates(0x1f8));
 }
 // HDMI capabilities are authoritative even if firmware's DVO label is DP.
 for(auto family:{Family::TigerLake,Family::AlderLakeN})for(unsigned mask=1;mask<4;++mask)
 for(unsigned a=1;a<=3;++a)for(unsigned b=1;b<=3;++b){
  std::vector<Port> p;if(mask&1)p.push_back({0x60d2,10,a,0,0,0});if(mask&2)p.push_back({0x60d2,7,b,0,0,0});
  auto bytes=table(p);auto r=parse(bytes.data(),bytes.size(),family);assert(r.valid());auto w=r.hdmi(3);
  if(mask==3 && a==b)assert(!w.valid);else {assert(w.valid&&w.hdmi==mask);assert(w.ddc[0]==((mask&1)?a:0));assert(w.ddc[1]==((mask&2)?b:0));}
 }
 auto bytes=table({{0x60d2,1,2,0,0xc0,0},{0x60d6,11,5,0x50,0xc0,3}});
 auto r=parse(bytes.data(),bytes.size(),Family::TigerLake);assert(r.valid()&&r.present==0x12&&r.hdmi(3).hdmi==2&&r.ambiguous==0x10);
 // Type-C capability never becomes an automatic fixed-DP mode decision.
 assert(r.port[4].capabilities==3&&r.port[4].dp());
 // All six port coordinates and capability combinations, independent of
 // CPU model. Runtime automatic fixed-DP backend covers native TC1..4.
 const unsigned directAUX[]={0x30,0x50,0x60,0x70,0x80,0x90};
 for(unsigned d=3;d<9;++d)for(unsigned caps=0;caps<4;++caps){
  const unsigned dvo[]={9,11,13,15,17,19};
  auto b=table({{0x60d6,dvo[d-3],d+1,directAUX[d-3],0xc0,caps}});
  auto q=parse(b.data(),b.size(),Family::TigerLake);
  assert(q.tglDPCandidates(0x1f8)==(1U<<d));
  assert(q.tglDPCandidates(0x18)==(d<5?1U<<d:0));
  assert(q.tglDPCandidates(0x78)==(d<7?1U<<d:0));
  for(unsigned aux:{0u,0x40u,0xa0u}){q.port[d].aux=aux;assert(!q.tglDPCandidates(0x1f8));}
 }
 for(auto p:{Port{0x60d2,11,5,0x50},Port{0x78c6,11,5,0x50},Port{0x60d6,11,5,0x50,2},
              Port{0x60d6,11,5,0x50,4},Port{0x60d6,11,5,0x50,1},Port{0x60d6,11,5,0x50,0x10}}){
  auto b=table({p});auto q=parse(b.data(),b.size(),Family::TigerLake);assert(!q.tglDPCandidates(0x18));
 }
 assert(r.tglDPCandidates(0x18)==0x10);
 auto shared=r;shared.present|=8;shared.port[3]={0x68c6,9,0,0x50};
 assert(!shared.tglDPCandidates(0x18));
 auto adl=table({{0x60d2,10,1},{0x60d2,7,2},{0x60d2,14,3}});
 auto desktop=table({{0x60d6,10,1,0x40},{0x60d2,1,2},{0x60d6,8,3,0x20},
                     {0x68c6,9,0,0x30},{1,0,0},{0x100,0,0}});
 auto ds=parse(desktop.data(),desktop.size(),Family::AlderLakeS);
 assert(ds.valid() && ds.present==0x39 && ds.port[3].fixedTMDS() && ds.port[3].ddc==2);
 // Same Display adapter across product families, without admitting a new GT.
 auto rs=parse(desktop.data(),desktop.size(),Family::RaptorLakeS);
 assert(rs.valid() && rs.present==ds.present && rs.unmapped==ds.unmapped && rs.ambiguous==ds.ambiguous);
 for(unsigned d=0;d<PortCount;++d)assert(!memcmp(&rs.port[d],&ds.port[d],sizeof(Port)));
 const unsigned desktopDP[]={10,7,8,9,11},desktopHD[]={0,1,2,3,12},physicalDDI[]={0,3,4,5,6};
 for(unsigned p=0;p<5;++p)for(auto dvo:{desktopDP[p],desktopHD[p]}) {
  assert(ddi(Family::RaptorLakeS,dvo)==physicalDDI[p]);
  auto v=table({{0x60d6,dvo,0,p?0x10*p:0x40}});
  auto parsed=parse(v.data(),v.size(),Family::RaptorLakeS);
  assert(parsed.valid() && parsed.present==(1U<<physicalDDI[p]));
 }
 for(unsigned dvo=0;dvo<256;++dvo)
  assert(ddi(Family::RaptorLakeS,dvo)==ddi(Family::AlderLakeS,dvo));
 for(unsigned dvo=0;dvo<256;++dvo)assert(ddi(Family::RaptorLakeP,dvo)==ddi(Family::AlderLakeP,dvo));
 for(auto family:{Family::Unknown,Family::RocketLake})
  assert(parse(desktop.data(),desktop.size(),family).status==Status::Platform);

 auto a=parse(adl.data(),adl.size(),Family::AlderLakeN);assert(a.valid()&&a.present==0xB&&a.hdmi(3).hdmi==3);
 assert(ddi(Family::TigerLake,9)==3&&ddi(Family::AlderLakeN,13)==3&&ddi(Family::AlderLakeN,19)==6);
 assert(ddi(Family::AlderLakeN,9)==PortCount&&ddi(Family::TigerLake,8)==PortCount);
 for(auto p:{Port{0x60d6,1,2},Port{0x78c6,10,1},Port{0x60d2,1,2,0,4},Port{0x60d2,1,2,0,2},Port{0x60d2,1,2,0,0,1}}){
  auto b=table({p});auto q=parse(b.data(),b.size(),Family::TigerLake);assert(q.valid()&&!q.hdmi(3).hdmi);
 }
 auto dup=table({{0x60d2,1,2},{0x60d2,7,2}});assert(parse(dup.data(),dup.size(),Family::TigerLake).status==Status::Conflict);
 for(size_t len=0;len<bytes.size();++len)assert(!parse(bytes.data(),len,Family::TigerLake).valid());
 // An otherwise valid firmware table can carry a stale checksum. Do not
 // lose its actual connector wiring or silently normalize the source data.
 auto bad=bytes;bad[26]=uint8_t(bad[26]+0x5a);const auto unchanged=bad;
 auto stale=parse(bad.data(),bad.size(),Family::TigerLake);
 assert(stale.valid()&&stale.checksum==0x5a&&stale.hdmi(3).hdmi==2&&stale.hdmi(3).ddc[1]==2);
 assert(bad==unchanged);
 // A checksum mismatch does not relax structural or semantic rejection.
 auto broken=bad;broken[28]=0xff;broken[29]=0xff;
 assert(parse(broken.data(),broken.size(),Family::TigerLake).status==Status::Header);
 broken=bad;broken[77]=1;
 assert(parse(broken.data(),broken.size(),Family::TigerLake).status==Status::Children);
 broken=dup;broken[26]^=0x5a;
 assert(parse(broken.data(),broken.size(),Family::TigerLake).status==Status::Conflict);
 for(unsigned off:{22u,24u,28u,66u,68u,71u,77u}){auto b=bytes;b[off]=0xff;b[off+1]=0xff;checksum(b);assert(!parse(b.data(),b.size(),Family::TigerLake).valid());}
 for(unsigned version:{215u,257u}){auto b=bytes;w16(b,64,version);checksum(b);assert(parse(b.data(),b.size(),Family::TigerLake).status==Status::Version);}
 auto empty=table({});assert(parse(empty.data(),empty.size(),Family::TigerLake).valid());
 assert(!selectHDMI(true,nullptr,0,r,3).valid);assert(selectHDMI(false,nullptr,0,r,3).hdmi==2);
 uint32_t explicitWiring[]={1,1,3,0,0};assert(selectHDMI(true,explicitWiring,20,r,3).hdmi==1);
 std::vector<uint8_t> op(8192);memcpy(op.data(),"IntelGraphicsMem",16);w32(op,16,8);op[23]=2;op[22]=1;w32(op,0x58,4);
 w32(op,0x3BA,8192);w32(op,0x3C2,4096);auto loc=region(op.data(),op.size(),0x80000000);
 assert(loc.valid&&loc.address==0x80002000&&loc.bytes==4096&&loc.inlineBytes==7168);
 op[22]=0;assert(region(op.data(),op.size(),0x80000000).address==8192);
 op[22]=1;w32(op,0x3BA,4096);assert(!region(op.data(),op.size(),0x80000000).address);
 w32(op,0x3BA,0xffffffff);w32(op,0x3BE,0xffffffff);assert(!region(op.data(),op.size(),0x80000000).address);
 w32(op,0x58,0x10);assert(region(op.data(),op.size(),0x80000000).inlineBytes==6144);
 assert(!region(op.data(),8191,0x80000000).valid);
 // Deterministic bounded mutation sweep: no reads outside declared buffers.
 unsigned seed=7;for(unsigned n=0;n<2000;++n){auto b=bytes;seed=seed*1664525+1013904223;b[seed%b.size()]^=uint8_t(seed>>24);checksum(b);(void)parse(b.data(),b.size(),Family::TigerLake);}
 // Optional panel blocks: malformed/missing panel data cannot remove HDMI.
 auto panelTable=table({{0x78C6,10,0,0x40},{0x60D2,1,2}});
 auto append=[](std::vector<uint8_t>&b,unsigned id,unsigned n) {
  auto at=b.size();b.resize(at+3+n);b[at]=id;w16(b,at+1,n);
  w16(b,24,b.size());w16(b,68,b.size()-48);return at+3;
 };
 auto options=append(panelTable,40,1);panelTable[options]=2;
 auto edp=append(panelTable,27,212);panelTable[edp+205]=1;
 auto power=append(panelTable,44,56);checksum(panelTable);
 auto panel=parse(panelTable.data(),panelTable.size(),Family::TigerLake);
 assert(panel.valid()&&panel.hdmi(3).hdmi==2&&panel.panel.valid);
 assert(panel.panel.type==2&&panel.panel.ddi==0&&!panel.panel.lowSwing&&!panel.panel.hobl);
 for(unsigned n=0;n<16;++n) {
  auto b=panelTable;b[options]=n;memset(b.data()+edp+204,0,8);
  w16(b,power+54,1U<<n);
  auto q=parse(b.data(),b.size(),Family::TigerLake);
  assert(q.panel.valid&&q.panel.lowSwing&&q.panel.hobl&&q.panel.type==n);
 }
 for(unsigned bad=0;bad<5;++bad) {
  auto b=panelTable;
  if(bad==0)b[options]=255;
  if(bad==1)w16(b,edp-2,211);
  if(bad==2)w16(b,power-2,55);
  if(bad==3)append(b,40,1);
  if(bad==4)w16(b,78+2,0x60D6); // no internal connector
  auto q=parse(b.data(),b.size(),Family::TigerLake);
  assert(q.valid()&&q.hdmi(3).hdmi==2&&!q.panel.valid);
 }
 for(size_t n=power;n<panelTable.size();++n) {
  auto b=panelTable;b.resize(n);w16(b,24,b.size());w16(b,68,b.size()-48);
  auto q=parse(b.data(),b.size(),Family::TigerLake);
  assert(q.valid()&&!q.panel.valid);
 }
 // PPS sequences select all five fields for all 16 panel types. Backlight
 // corruption is independent of signal/PPS facts and unrelated connectors.
 auto bl=append(panelTable,43,129);panelTable[bl]=6;
 for(unsigned n=0;n<16;++n) {
  for(unsigned i=0;i<5;++i)w16(panelTable,edp+10*n+2*i,100*n+i+1);
  panelTable[bl+1+6*n]=6;w16(panelTable,bl+2+6*n,200+n);
  panelTable[bl+113+n]=uint8_t(n<<4|2);
 }
 for(unsigned n=0;n<16;++n) {
  auto b=panelTable;b[options]=n;auto q=parse(b.data(),b.size(),Family::TigerLake);
  assert(q.panel.valid && q.panel.backlightValid && q.panel.pwmPresent && q.panel.activeLowPWM);
  assert(q.panel.controller==n && q.panel.backlightType==2 && q.panel.pwmHz==200+n);
  for(unsigned i=0;i<5;++i)assert(q.panel.delays[i]==100*n+i+1);
 }
 for(unsigned bad=0;bad<3;++bad) {
  auto b=panelTable;
  if(bad==0)b[bl]=7;
  if(bad==1)w16(b,bl-2,128);
  if(bad==2)append(b,43,129);
  auto q=parse(b.data(),b.size(),Family::TigerLake);
  assert(q.panel.valid && !q.panel.backlightValid && q.hdmi(3).hdmi==2);
 }
 for(size_t n=bl;n<panelTable.size();++n) {
  auto b=panelTable;b.resize(n);w16(b,24,b.size());w16(b,68,b.size()-48);
  auto q=parse(b.data(),b.size(),Family::TigerLake);
  assert(q.panel.valid && !q.panel.backlightValid);
 }
 puts("PASS firmware bounds, checksum diagnostics, connector maps, optional panel facts and OpRegion addresses");
}
