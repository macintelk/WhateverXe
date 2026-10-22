// Tiger Lake display register coordinates, not connector availability.
// Derived from Linux v6.12 intel_dkl_phy_regs.h, intel_mg_phy_regs.h,
// i915_reg.h and intel_tc.c. TGL only: do not
// reuse this numbering for ICL, ADL-P, RKL or DG1.
#ifndef kern_tgl_port_map_hpp
#define kern_tgl_port_map_hpp

#include <stdint.h>

struct TGLComboPLLFraction {
 // Display WA 22010492432 (Linux intel_dpll_mgr.c): TGL with a
 // 38.4 MHz reference needs half the nominal combo DCO fraction.
 // refCode is the donor's unmodified DSSM[31:29], not RAWCLK.
 static bool adjust(uint32_t refCode, unsigned long reg, uint32_t &value) {
  if (refCode != 2 || (reg != 0x164284 && reg != 0x16428C)) return false;
  constexpr uint32_t mask = 0x7FFFU << 10;
  const auto fraction = ((value & mask) >> 10);
  value = (value & ~mask) | (((fraction + 1) / 2) << 10);
  return true;
 }
};

struct TGLDPAltProbe {
 // GEN11_DE_HPD_ISR has separate TC and TBT live signals. Use the same
 // always-accessible HPD banks as native getHPDState, not an unowned FIA read.
 static uint32_t mode(uint32_t ddi,uint32_t selected,uint32_t sde,uint32_t hpd) {
  if(ddi<3 || ddi>8 || !(selected&(1U<<ddi)) || sde==UINT32_MAX || hpd==UINT32_MAX)return 0;
  const auto tc=ddi-3;
  return !(sde&(1U<<(24+tc))) && !(hpd&(1U<<tc)) && (hpd&(1U<<(16+tc))) ? 3 : 0;
 }
};

struct TGLDklDPMode {
 // Linux icl_program_mg_dp_mode: pin assignment, not orientation/lane mask,
 // determines the x1/x2 mux setting on both DKL lane groups.
 static bool select(uint32_t pin,uint32_t lanes,uint32_t (&mode)[2]) {
  mode[0]=mode[1]=0;
  if((lanes!=1 && lanes!=2 && lanes!=4) || pin>6)return false;
  switch(pin) {
   case 0: if(lanes==1)mode[1]=0x40;else mode[0]=mode[1]=0x80;break;
   case 1: if(lanes==4)mode[0]=mode[1]=0x80;break;
   case 2: if(lanes==2)mode[0]=mode[1]=0x80;break;
   default:mode[0]=mode[1]=lanes==1?0x40:0x80;break;
  }
  return true;
 }
};

struct TGLDPAltLaneEnable {
 // DFLEXDPMLE selects logical main-link lanes. USB-C orientation is
 // handled by hardware; DFLEXDPSP's physical lane assignment is not this
 // mask. Linux intel_tc_port_set_fia_lane_count uses ML0/ML1_0/ML3_0.
 static bool update(uint32_t ddi,uint32_t lanes,uint32_t &value) {
  if(ddi<3 || ddi>4 || value==UINT32_MAX ||
     (lanes!=1 && lanes!=2 && lanes!=4))return false;
  const auto shift=4*(ddi-3);
  value=(value&~(0xFU<<shift))|(((1U<<lanes)-1)<<shift);
  return true;
 }
};

struct TGLDPAltSignal {
 // PHY coefficients address physical lanes, unlike DFLEXDPMLE. Restrict
 // writes to the FIA-assigned lanes; the other pair can belong to USB.
 template<typename Access>
 static bool program(uint32_t mask,uint32_t lanes,uint32_t rate,
                     uint32_t swing,uint32_t emphasis,Access access) {
  if(!mask || mask>15 || (lanes!=1 && lanes!=2 && lanes!=4) ||
     (rate!=6 && rate!=10 && rate!=20) || swing>3 || emphasis>3 || swing+emphasis>3)return false;
  unsigned count=0;for(unsigned bit=0;bit<4;++bit)count+=(mask>>bit)&1;
  if(count<lanes)return false;
  static const uint32_t table[10][3] {
   {7,0,0},{5,0,5},{2,0,11},{0,0,25},{5,0,0},
   {2,0,8},{0,0,20},{2,0,0},{0,0,11},{0,0,0}
  };
  static const unsigned start[]={0,4,7,9};
  const auto level=start[swing]+emphasis;
  const auto post=(rate<=10 && level==3)?24:table[level][2];
  const auto coeff=table[level][0]|(table[level][1]<<13)|(post<<8);
  for(unsigned ln=0;ln<2;++ln) {
   if(!(mask&(3U<<(2*ln))))continue;
   for(unsigned tx=0;tx<3;++tx) {
    if(tx<2 && !(mask&(1U<<(2*ln+tx))))continue;
    const auto index=0x42+3*ln+tx;
    uint32_t value=0;
    if(!access(index,false,value) || value==UINT32_MAX)return false;
    const auto wanted=tx==2?(value&~4U):((value&~0x3FF07U)|coeff);
    value=wanted;
    if(!access(index,true,value) || !access(index,false,value) || value!=wanted)return false;
   }
  }
  return true;
 }
};

struct TGLComboAUXGate {
 // The donor toggles ICL LANE_ENABLE_AUX in its AUX power-well method.
 // TGL uses the AUX power-well request/ACK without this PHY override.
 static bool legacyWrite(unsigned long reg, uintptr_t caller) {
  return (reg == 0x162030 && (caller == 0x5CAB6 || caller == 0x5D0C9)) ||
         (reg == 0x6C030 && (caller == 0x5CAF5 || caller == 0x5D108));
 }
};

struct TGLFiaPort {
 uint32_t instance {}, index {}, live {}, ready {}, owned {}, laneSelect {};
 uint32_t readyMask {}, altMask {}, laneSelectMask {}, laneSelectShift {};
 // Classify values already sampled under the caller's power contract.
 bool readyForLegacy(uint32_t liveValue, uint32_t readyValue) const {
  return readyMask && liveValue != 0xFFFFFFFFU && readyValue != 0xFFFFFFFFU &&
   (readyValue & readyMask) && !(liveValue & altMask);
 }
};

struct TGLTypeCPort {
 uint32_t ddi {}, tcIndex {}, phyBase {}, hipRegister {}, hipShift {};
 uint32_t pllEnable {}, ddiBuffer {};

 static bool resolve(uint32_t physicalDDI, TGLTypeCPort &out) {
  out = {};
  // Apple donor DDI3..8 map to TGL TC1..6. This does not qualify any PHY
  // backend or imply that a connector is wired on a particular machine.
  if (physicalDDI < 3 || physicalDDI > 8) return false;
  const auto tc = physicalDDI - 3;
  out.ddi = physicalDDI; out.tcIndex = tc;
  out.phyBase = 0x168000 + 0x1000 * tc;
  out.hipRegister = tc < 4 ? 0x1010A0 : 0x1010A4;
  out.hipShift = 8 * (tc % 4);
  out.pllEnable = 0x46030 + 4 * tc;
  out.ddiBuffer = 0x64000 + 0x100 * physicalDDI;
  return true;
 }

 bool legacyHPD(uint32_t sdeisr) const {
  return ddi >= 3 && ddi <= 8 && tcIndex == ddi - 3 &&
   sdeisr != 0xFFFFFFFFU && (sdeisr & (1U << (24 + tcIndex)));
 }

 uint32_t phyRegister(uint32_t internal) const {
  return phyBase + (internal & 0xFFF);
 }
 uint32_t selectBank(uint32_t previous, uint32_t internal) const {
  return (previous & ~(0xFU << hipShift)) |
   (((internal >> 12) & 0xF) << hipShift);
 }

 // The caller must sample FIA1 under its own TC-cold/power contract. Never
 // discover FIA topology by poking unpowered registers in this resolver.
 bool resolveFia(uint32_t fia1Live, TGLFiaPort &out) const {
  out = {};
  if (ddi < 3 || ddi > 8 || tcIndex != ddi - 3 || fia1Live == 0xFFFFFFFFU)
   return false;
  const bool modular = fia1Live & (1U << 4);
  const auto instance = modular ? tcIndex / 2 : 0;
  const auto index = modular ? tcIndex % 2 : tcIndex;
  // A non-modular 32-bit register has only four eight-bit port fields.
  if (index >= 4) return false;
  const uint32_t bases[] {0x163000, 0x16E000, 0x16F000};
  const auto base = bases[instance];
  out.instance = instance; out.index = index;
  out.live = base + 0x8A0; out.ready = base + 0x890;
  out.owned = base + 0x894; out.laneSelect = base + 0x8C0;
  out.readyMask = 1U << index;
  out.altMask = 0x60U << (8 * index);
  out.laneSelectShift = 4 * index; out.laneSelectMask = 0xFU << out.laneSelectShift;
  return true;
 }
};

// A native SST path may use a combo eDP port or a qualified legacy TC port.
// This selects register coordinates, not firmware wiring or a fixed pipe.
struct TGLSSTTrainingPort {
 uint32_t ddi {}, ddiBuffer {};
 bool combo {};
 static bool resolve(uint32_t physicalDDI, uint32_t kind, uint32_t backend,
                     bool qualifiedTC, TGLSSTTrainingPort &out) {
  out = {};
  const bool edp = physicalDDI < 2 && kind == 0 && backend == 1;
  const bool tc = physicalDDI >= 3 && physicalDDI <= 8 &&
   kind == 1 && (backend == 3 || backend == 4) && qualifiedTC;
  if (!edp && !tc) return false;
  out.ddi = physicalDDI; out.ddiBuffer = 0x64000 + 0x100 * physicalDDI;
  out.combo = edp;
  return true;
 }
 uint32_t mainPowerMask(uint32_t pipe) const {
  // TGL PW1 owns combo lanes and pipe A; PW3 adds pipe B / TC lanes,
  // and PW4 adds pipe C (Linux v6.12 intel_display_power_map.c).
  return pipe == 2 ? 0xFFU : (pipe == 0 && combo ? 3U : 0x3FU);
 }
};

#endif
