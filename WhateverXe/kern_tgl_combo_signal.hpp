// TGL combo PHY programming. Register fields and electrical coefficients:
// Intel Linux v6.12 intel_combo_phy_regs.h / intel_ddi_buf_trans.c (MIT).
// Independently expressed bounded sequence; no board or panel-ID overrides.
//
// Reference coefficient tables: Copyright (c) 2020 Intel Corporation.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#ifndef KERN_TGL_COMBO_SIGNAL_HPP
#define KERN_TGL_COMBO_SIGNAL_HPP
#include <stdint.h>
struct TGLComboSignal {
    uint32_t base {},lanes {},level {},swing {},scalar {},cursor {},post1 {};
    bool low {};
    bool select(uint32_t ddi,uint32_t count,uint32_t rate,bool lowSwing,bool hobl,
                uint32_t voltage,uint32_t emphasis) {
        // Normal HBR2 has a separate U/Y-family table. Leave that unreviewed
        // path, HOBL and HBR3 with the donor until their contracts are covered.
        if(ddi>1 || (count!=1 && count!=2 && count!=4) || hobl ||
           (rate!=6 && rate!=10 && !(lowSwing && rate==20)) ||
           voltage>3 || emphasis>3 || voltage+emphasis>3)return false;
        static const uint8_t first[]={0,4,7,9};
        static const uint8_t normal[10][4]={
            {0xA,0x32,0x3F,0},{0xA,0x4F,0x37,8},{0xC,0x71,0x2F,0x10},{6,0x7D,0x2B,0x14},
            {0xA,0x4C,0x3F,0},{0xC,0x73,0x34,0xB},{6,0x7F,0x2F,0x10},
            {0xC,0x6C,0x3C,3},{6,0x7F,0x35,0xA},{6,0x7F,0x3F,0}
        };
        static const uint8_t reduced[10][4]={
            {0,0x7F,0x3F,0},{8,0x7F,0x38,7},{1,0x7F,0x33,0xC},{9,0x7F,0x31,0xE},
            {8,0x7F,0x3F,0},{1,0x7F,0x38,7},{9,0x7F,0x35,0xA},
            {1,0x7F,0x3F,0},{9,0x7F,0x38,7},{9,0x7F,0x3F,0}
        };
        base=ddi ? 0x6C000:0x162000;lanes=count;level=first[voltage]+emphasis;low=lowSwing;
        const auto row=low ? reduced[level]:normal[level];
        swing=row[0];scalar=row[1];cursor=row[2];post1=row[3];return true;
    }
    // Caller owns this live port's power. Pre-read all lane words before any
    // write; refuse an inaccessible bank without a partial programming sequence.
    template<class Read,class Write> bool program(Read read,Write write) const {
        uint32_t pcs=read(base+0x804),cl5=read(base+0x14),cl10=read(base+0x28),
            dw5=read(base+0x894),dw2[4],dw4[4],dw7[4];
        if((pcs==~0U)||(cl5==~0U)||(cl10==~0U)||(dw5==~0U))return false;
        for(unsigned n=0;n<4;++n) {
            dw2[n]=read(base+0x888+0x100*n);
            dw4[n]=read(base+0x890+0x100*n);
            dw7[n]=read(base+0x89C+0x100*n);
            if(dw2[n]==~0U || dw4[n]==~0U || dw7[n]==~0U)return false;
        }
        write(base+0x604,pcs|(1U<<26));
        for(unsigned n=0;n<4;++n) {
            const bool load=n==1 || n==2 || (lanes==4 && n==3);
            dw4[n]=(dw4[n]&~0x80000000U)|(load?0x80000000U:0);
            write(base+0x890+0x100*n,dw4[n]);
        }
        write(base+0x14,cl5|3U);
        dw5&=~0x80000000U;write(base+0x694,dw5);
        cl10&=~0xCU;write(base+0x28,cl10); // non-HOBL
        dw5=(dw5&~0x601C0038U)|0x20080030U;
        write(base+0x694,dw5);
        for(unsigned n=0;n<4;++n)
            write(base+0x888+0x100*n,(dw2[n]&~0xB8FFU)|
                ((swing&8)<<12)|((swing&7)<<11)|0x98U);
        for(unsigned n=0;n<4;++n)
            write(base+0x890+0x100*n,(dw4[n]&~0x3FFFFU)|(post1<<12)|cursor);
        for(unsigned n=0;n<4;++n)
            write(base+0x89C+0x100*n,(dw7[n]&~0x7F000000U)|(scalar<<24));
        write(base+0x694,dw5|0x80000000U);
        // Preserve the donor's final lane power-up for non-reversed SST.
        const uint32_t laneMask=lanes==1?0xE0U:lanes==2?0xC0U:0;
        write(base+0x28,(cl10&~0xF0U)|laneMask);
        bool ok=(read(base+0x894)&0xE01C0038U)==0xA0080030U &&
            (read(base+0x28)&0xFCU)==laneMask;
        for(unsigned n=0;n<4;++n) {
            ok&=(read(base+0x888+0x100*n)&0xB8FFU)==(((swing&8)<<12)|((swing&7)<<11)|0x98U);
            ok&=(read(base+0x890+0x100*n)&0x3FFFFU)==((post1<<12)|cursor);
            ok&=(read(base+0x89C+0x100*n)&0x7F000000U)==(scalar<<24);
        }
        return ok;
    }
};
#endif
