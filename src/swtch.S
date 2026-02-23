.globl swtch
swtch:
    # swtch(struct context **old, struct context *new)
    # rdi = old, rsi = new
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, (%rdi)      # *old = rsp (save old context pointer)
    movq %rsi, %rsp         # rsp = new  (load new context)

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    retq                    # pops rip from new context
