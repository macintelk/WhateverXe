// EDID 1.4 digital-input component depth, applied before native DP timing math.
#ifndef KERN_TGL_PANEL_DEPTH_HPP
#define KERN_TGL_PANEL_DEPTH_HPP
#include <stdint.h>
#include <stddef.h>
struct TGLPanelDepth {
 static uint32_t maximum(const uint8_t *edid,size_t length) {
  static const uint8_t header[]={0,255,255,255,255,255,255,0};
  if(!edid || length!=128)return 0;
  unsigned sum=0;
  for(unsigned i=0;i<128;++i)sum+=edid[i];
  for(unsigned i=0;i<8;++i)if(edid[i]!=header[i])return 0;
  if((sum&255) || edid[18]!=1 || edid[19]!=4 || !(edid[20]&128) ||
     (edid[20]&15)!=5)return 0; // Explicit DisplayPort digital interface.
  const unsigned code=(edid[20]>>4)&7;
  return code && code<7 ? 4+2*code : 0;
 }
 static uint16_t clamp(uint16_t encoding,uint16_t requested,uint32_t maximum) {
  // IODetailedTimingInformationV2 uses single-bit selections, not bit counts.
  // Keep unknown/multibit requests and non-RGB timings with the donor.
  const uint16_t bits[]={6,8,10,12,16};
  if(encoding!=1 || !maximum)return requested;
  for(unsigned i=0;i<5;++i)if(requested==(1U<<i)) {
   if(bits[i]<=maximum)return requested;
   for(int j=int(i)-1;j>=0;--j)if(bits[j]<=maximum)return 1U<<j;
  }
  return requested;
 }
};
#endif
