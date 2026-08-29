.text
.globl copy_and_swap
.type copy_and_swap, @function

copy_and_swap:
    lr.d t0, (a0)
    bne t0, a1, fail
    sc.d t1, a2, (a0)
    bnez t1, copy_and_swap
fail:
    mv a0, t0
    ret