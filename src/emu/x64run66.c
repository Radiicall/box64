#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include "debug.h"
#include "box64stack.h"
#include "x64emu.h"
#include "x64run.h"
#include "x64emu_private.h"
#include "x64run_private.h"
#include "x64primop.h"
#include "x64trace.h"
#include "x87emu_private.h"
#include "box64context.h"
#include "bridge.h"
//#include "signals.h"
#ifdef DYNAREC
#include "../dynarec/arm_lock_helper.h"
#endif

#include "modrm.h"

int Run66(x64emu_t *emu, rex_t rex)
{
    uint8_t opcode;
    uint8_t nextop;
    uint8_t tmp8u;
    int16_t tmp16s;
    uint16_t tmp16u;
    uint64_t tmp64u;
    reg64_t *oped, *opgd;

    opcode = F8;

    while((opcode==0x2E) || (opcode==0x66))   // ignoring CS: or multiple 0x66
        opcode = F8;

    // REX prefix before the F0 are ignored
    rex.rex = 0;
    while(opcode>=0x40 && opcode<=0x4f) {
        rex.rex = opcode;
        opcode = F8;
    }

    switch(opcode) {
    #define GO(B, OP)                                               \
    case B+0:                                                       \
        nextop = F8;                                                \
        GETEB(0);                                                   \
        GETGB;                                                      \
        EB->byte[0] = OP##8(emu, EB->byte[0], GB);                  \
        break;                                                      \
    case B+1:                                                       \
        nextop = F8;                                                \
        GETEW(0);                                                   \
        GETGW;                                                      \
        if(rex.w)                                                   \
            EW->q[0] = OP##64(emu, EW->q[0], GW->q[0]);             \
        else                                                        \
            EW->word[0] = OP##16(emu, EW->word[0], GW->word[0]);    \
        break;                                                      \
    case B+2:                                                       \
        nextop = F8;                                                \
        GETEB(0);                                                   \
        GETGB;                                                      \
        GB = OP##8(emu, GB, EB->byte[0]);                           \
        break;                                                      \
    case B+3:                                                       \
        nextop = F8;                                                \
        GETEW(0);                                                   \
        GETGW;                                                      \
        if(rex.w)                                                   \
            GW->q[0] = OP##64(emu, GW->q[0], EW->q[0]);             \
        else                                                        \
        GW->word[0] = OP##16(emu, GW->word[0], EW->word[0]);        \
        break;                                                      \
    case B+4:                                                       \
        R_AL = OP##8(emu, R_AL, F8);                                \
        break;                                                      \
    case B+5:                                                       \
        if(rex.w)                                                   \
            R_RAX = OP##64(emu, R_RAX, F32S64);                     \
        else                                                        \
            R_AX = OP##16(emu, R_AX, F16);                          \
        break;

    GO(0x00, add)                   /* ADD 0x01 ~> 0x05 */
    GO(0x08, or)                    /*  OR 0x09 ~> 0x0D */
    GO(0x10, adc)                   /* ADC 0x11 ~> 0x15 */
    GO(0x18, sbb)                   /* SBB 0x19 ~> 0x1D */
    GO(0x20, and)                   /* AND 0x21 ~> 0x25 */
    GO(0x28, sub)                   /* SUB 0x29 ~> 0x2D */
    GO(0x30, xor)                   /* XOR 0x31 ~> 0x35 */

    case 0x0F:                              /* more opcdes */
        return Run660F(emu, rex);

    case 0x39:
        nextop = F8;
        GETEW(0);
        GETGW;
        if(rex.w)
            cmp64(emu, EW->q[0], GW->q[0]);
        else
            cmp16(emu, EW->word[0], GW->word[0]);
        break;
    case 0x3B:
        nextop = F8;
        GETEW(0);
        GETGW;
        if(rex.w)
            cmp64(emu, GW->q[0], EW->dword[0]);
        else
            cmp16(emu, GW->word[0], EW->word[0]);
        break;
    case 0x3D:
        if(rex.w)
            cmp64(emu, R_RAX, F32S64);
        else
            cmp16(emu, R_AX, F16);
        break;

    case 0x64:                              /* FS: */
        return Run6664(emu, rex);

    case 0x81:                              /* GRP3 Ew,Iw */
    case 0x83:                              /* GRP3 Ew,Ib */
        nextop = F8;
        GETEW((opcode==0x81)?2:1);
        GETGW;
        if(opcode==0x81) 
            tmp16u = F16;
        else {
            tmp16s = F8S;
            tmp16u = (uint16_t)tmp16s;
        }
        switch((nextop>>3)&7) {
            case 0: EW->word[0] = add16(emu, EW->word[0], tmp16u); break;
            case 1: EW->word[0] =  or16(emu, EW->word[0], tmp16u); break;
            case 2: EW->word[0] = adc16(emu, EW->word[0], tmp16u); break;
            case 3: EW->word[0] = sbb16(emu, EW->word[0], tmp16u); break;
            case 4: EW->word[0] = and16(emu, EW->word[0], tmp16u); break;
            case 5: EW->word[0] = sub16(emu, EW->word[0], tmp16u); break;
            case 6: EW->word[0] = xor16(emu, EW->word[0], tmp16u); break;
            case 7:               cmp16(emu, EW->word[0], tmp16u); break;
        }
        break;

    case 0x85:                              /* TEST Ew,Gw */
        nextop = F8;
        GETEW(0);
        GETGW;
        test16(emu, EW->word[0], GW->word[0]);
        break;

    case 0x89:                              /* MOV Ew,Gw */
        nextop = F8;
        GETEW(0);
        GETGW;
        EW->word[0] = GW->word[0];
        break;

    case 0x90:                              /* NOP */
        break;

    case 0xB8:                              /* MOV AX,Iw */
    case 0xB9:                              /* MOV CX,Iw */
    case 0xBA:                              /* MOV DX,Iw */
    case 0xBB:                              /* MOV BX,Iw */
    case 0xBC:                              /*    ...     */
    case 0xBD:
    case 0xBE:
    case 0xBF:
        if(rex.w)
            emu->regs[(opcode&7)+(rex.b<<3)].q[0] = F64;
        else
            emu->regs[(opcode&7)+(rex.b<<3)].word[0] = F16;
        break;

    case 0xC1:                              /* GRP2 Ew,Ib */
        nextop = F8;
        GETEW(1);
        tmp8u = F8 /*& 0x1f*/;
        switch((nextop>>3)&7) {
            case 0: EW->word[0] = rol16(emu, EW->word[0], tmp8u); break;
            case 1: EW->word[0] = ror16(emu, EW->word[0], tmp8u); break;
            case 2: EW->word[0] = rcl16(emu, EW->word[0], tmp8u); break;
            case 3: EW->word[0] = rcr16(emu, EW->word[0], tmp8u); break;
            case 4:
            case 6: EW->word[0] = shl16(emu, EW->word[0], tmp8u); break;
            case 5: EW->word[0] = shr16(emu, EW->word[0], tmp8u); break;
            case 7: EW->word[0] = sar16(emu, EW->word[0], tmp8u); break;
        }
        break;

    case 0xC7:                              /* MOV Ew,Iw */
        nextop = F8;
        GETEW(2);
        EW->word[0] = F16;
        break;

    case 0xD1:                              /* GRP2 Ew,1  */
    case 0xD3:                              /* GRP2 Ew,CL */
        nextop = F8;
        GETEW(0);
        tmp8u=(opcode==0xD3)?R_CL:1;
        if(rex.w) {
            switch((nextop>>3)&7) {
                case 0: EW->q[0] = rol64(emu, EW->q[0], tmp8u); break;
                case 1: EW->q[0] = ror64(emu, EW->q[0], tmp8u); break;
                case 2: EW->q[0] = rcl64(emu, EW->q[0], tmp8u); break;
                case 3: EW->q[0] = rcr64(emu, EW->q[0], tmp8u); break;
                case 4: 
                case 6: EW->q[0] = shl64(emu, EW->q[0], tmp8u); break;
                case 5: EW->q[0] = shr64(emu, EW->q[0], tmp8u); break;
                case 7: EW->q[0] = sar64(emu, EW->q[0], tmp8u); break;
            }
        } else {
            switch((nextop>>3)&7) {
                case 0: EW->word[0] = rol16(emu, EW->word[0], tmp8u); break;
                case 1: EW->word[0] = ror16(emu, EW->word[0], tmp8u); break;
                case 2: EW->word[0] = rcl16(emu, EW->word[0], tmp8u); break;
                case 3: EW->word[0] = rcr16(emu, EW->word[0], tmp8u); break;
                case 4: 
                case 6: EW->word[0] = shl16(emu, EW->word[0], tmp8u); break;
                case 5: EW->word[0] = shr16(emu, EW->word[0], tmp8u); break;
                case 7: EW->word[0] = sar16(emu, EW->word[0], tmp8u); break;
            }
        }
        break;

    case 0xF7:                      /* GRP3 Ew(,Iw) */
        nextop = F8;
        tmp8u = (nextop>>3)&7;
        if(rex.w) {
            GETED((tmp8u<2)?4:0);
            switch(tmp8u) {
                case 0: 
                case 1:                 /* TEST Ed,Id */
                    tmp64u = F32S64;
                    test64(emu, ED->q[0], tmp64u);
                    break;
                case 2:                 /* NOT Ed */
                    ED->q[0] = not64(emu, ED->q[0]);
                    break;
                case 3:                 /* NEG Ed */
                    ED->q[0] = neg64(emu, ED->q[0]);
                    break;
                case 4:                 /* MUL RAX,Ed */
                    mul64_rax(emu, ED->q[0]);
                    break;
                case 5:                 /* IMUL RAX,Ed */
                    imul64_rax(emu, ED->q[0]);
                    break;
                case 6:                 /* DIV Ed */
                    div64(emu, ED->q[0]);
                    break;
                case 7:                 /* IDIV Ed */
                    idiv64(emu, ED->q[0]);
                    break;
            }
        } else {
            switch(tmp8u) {
                case 0: 
                case 1:                 /* TEST Ew,Iw */
                    GETEW(2);
                    test16(emu, EW->word[0], F16);
                    break;
                case 2:                 /* NOT Ew */
                    GETEW(0);
                    EW->word[0] = not16(emu, EW->word[0]);
                    break;
                case 3:                 /* NEG Ew */
                    GETEW(0);
                    EW->word[0] = neg16(emu, EW->word[0]);
                    break;
                case 4:                 /* MUL AX,Ew */
                    GETEW(0);
                    mul16(emu, EW->word[0]);
                    break;
                case 5:                 /* IMUL AX,Ew */
                    GETEW(0);
                    imul16_eax(emu, EW->word[0]);
                    break;
                case 6:                 /* DIV Ew */
                    GETEW(0);
                    div16(emu, EW->word[0]);
                    break;
                case 7:                 /* IDIV Ew */
                    GETEW(0);
                    idiv16(emu, EW->word[0]);
                    break;
            }
        }
        break;

        default:
            return 1;
    }
    return 0;
}