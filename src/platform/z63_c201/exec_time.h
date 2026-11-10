#ifndef FX_Z63_EXEC_TIME_H
#define FX_Z63_EXEC_TIME_H
/* Protected EL0 counter accounting. Keep all established gate offsets stable. */
#define FX_Z63_ACTIVE_START 1136
#define FX_Z63_ACTIVE_TICKS 1144
#define FX_Z63_ACTIVE_LIMIT 1152
#ifdef __ASSEMBLER__
/* Exception entry has already saved EL0 state. x4 retains the SVC result.
 * Clamp overflow/over-budget totals so a wrap cannot restore admission. */
.macro fx_z63_charge context
    mrs x5,cntpct_el0
    ldr x6,[\context,#FX_Z63_ACTIVE_START]
    sub x5,x5,x6
    ldr x6,[\context,#FX_Z63_ACTIVE_TICKS]
    adds x5,x5,x6
    ldr x7,[\context,#FX_Z63_ACTIVE_LIMIT]
    csel x5,x7,x5,cs
    cmp x5,x7
    csel x5,x7,x5,hi
    str x5,[\context,#FX_Z63_ACTIVE_TICKS]
.endm
#endif
#endif
