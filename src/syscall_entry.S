.globl syscall_entry
syscall_entry:
    # On entry from syscall instruction:
    #   RCX = user RIP, R11 = user RFLAGS, RAX = syscall number
    #   RDI,RSI,RDX,R10,R8,R9 = args 1-6
    #   Interrupts are masked (FMASK cleared IF)

    # Save user RSP, switch to kernel stack
    movq %rsp, scratch_rsp(%rip)
    movq kernel_rsp(%rip), %rsp

    # Save user return state and callee-preserved registers
    pushq scratch_rsp(%rip)     # user RSP
    pushq %rcx                  # user RIP
    pushq %r11                  # user RFLAGS
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    # Save argument registers (caller-saved, user expects preserved)
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %r10
    pushq %r8
    pushq %r9

    # 15 pushes = 120 bytes from 16-aligned kernel_rsp
    # 120 % 16 = 8, so RSP is 8 mod 16. Need sub 8 for alignment.
    subq $8, %rsp

    # Map syscall convention to C calling convention:
    #   syscall: RAX=num, RDI=a1, RSI=a2, RDX=a3, R10=a4, R8=a5
    #   C call:  RDI=num, RSI=a1, RDX=a2, RCX=a3, R8=a4, R9=a5
    movq %r8, %r9               # a5 -> r9
    movq %r10, %r8              # a4 -> r8
    movq %rdx, %rcx             # a3 -> rcx
    movq %rsi, %rdx             # a2 -> rdx
    movq %rdi, %rsi             # a1 -> rsi
    movq %rax, %rdi             # num -> rdi

    call syscall_handler
    # RAX = return value

    addq $8, %rsp               # remove alignment padding

    # Restore argument registers
    popq %r9
    popq %r8
    popq %r10
    popq %rdx
    popq %rsi
    popq %rdi

    # Restore callee-preserved registers
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx

    popq %r11                   # user RFLAGS
    popq %rcx                   # user RIP
    movq (%rsp), %rsp           # restore user RSP

    sysretq
