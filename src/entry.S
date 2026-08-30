.section .entry_os
.global _entry
.type _entry, @function

_entry:
    la sp, stack_top

    # Delegiraj U-mode i S-mode ecall izuzetke u S-mode
    li t0, (1 << 8) | (1 << 9)
    csrw medeleg, t0

    # Delegiraj supervisor external interrupt u S-mode
    li t0, (1 << 9)
    csrw mideleg, t0

    # mstatus.MPP = Supervisor mode (01)
    csrr t0, mstatus
    li t1, ~(3 << 11)
    and t0, t0, t1
    li t1, (1 << 11)
    or t0, t0, t1
    csrw mstatus, t0

    # mepc = main
    la t0, main
    csrw mepc, t0

    # prelazak M-mode -> S-mode
    mret

1:
    j 1b


.section .bss
.align 12
stack:
    .space 4096
stack_top: