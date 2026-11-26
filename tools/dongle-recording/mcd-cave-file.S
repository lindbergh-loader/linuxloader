# Minecraft Dungeons Arcade cave, FILE variant: write the dongle records to
# "mcdrec.bin" in the game's cwd instead of fd 2, so a TeknoParrotUi launch
# (detached console) still captures them. Trampolines identical to mcd-cave.S
# (enc at +0, dec at +0xF). split-records.py --status-bytes 4.
    .code64
    .set WRITESLOT, 0x16d3b40
    .set OPENSLOT,  0x16d3110       # the game's open import slot
    .set HENC, 0xb66cbd
    .set HDEC, 0xb66dcf
    .globl _start
_start:
    mov $HENC,%r10d
    lea tag_enc(%rip),%r11
    jmp body
    mov $HDEC,%r10d
    lea tag_dec(%rip),%r11
body:
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    sub $24,%rsp
    mov %edi,%r12d
    mov %rsi,%r13
    mov %edx,%r14d
    mov %r10,%rbx
    mov %r11,%rbp
    mov %r14d,(%rsp)
    # open mcdrec.bin (O_WRONLY|O_CREAT|O_APPEND, 0644) -> fd at 8(%rsp)
    lea fname(%rip),%rdi
    mov $0x441,%esi
    mov $0x1a4,%edx
    mov OPENSLOT,%rax
    call *%rax
    mov %eax,8(%rsp)
    # marker
    mov 8(%rsp),%edi
    mov %rbp,%rsi
    mov $4,%edx
    mov WRITESLOT,%rax
    call *%rax
    # length
    mov 8(%rsp),%edi
    mov %rsp,%rsi
    mov $4,%edx
    mov WRITESLOT,%rax
    call *%rax
    # input
    mov 8(%rsp),%edi
    mov %r13,%rsi
    mov %r14d,%edx
    mov WRITESLOT,%rax
    call *%rax
    # real call
    mov %r12d,%edi
    mov %r13,%rsi
    mov %r14d,%edx
    call *%rbx
    mov %eax,%r15d
    mov %eax,4(%rsp)
    # status
    mov 8(%rsp),%edi
    lea 4(%rsp),%rsi
    mov $4,%edx
    mov WRITESLOT,%rax
    call *%rax
    # output
    mov 8(%rsp),%edi
    mov %r13,%rsi
    mov %r14d,%edx
    mov WRITESLOT,%rax
    call *%rax
    mov %r15d,%eax
    add $24,%rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    ret
fname: .asciz "mcdrec.bin"
tag_enc: .ascii "@@E@"
tag_dec: .ascii "@@D@"
