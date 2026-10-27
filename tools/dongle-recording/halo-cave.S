# Halo Fireteam Raven (64-bit): log the dongle's decrypt calls.
#
#   as --64 halo-cave.S -o halo-cave.o
#   ld -Ttext=0x1461100 -e _start -o halo-cave.elf halo-cave.o
#   objcopy -O binary -j .text halo-cave.elf halo-cave.bin    # 160 bytes
#
# written at file offset 0x1061100, into the spare bytes at the end of the
# code segment, with the call at 0x6398e7 repointed from hasp_decrypt
# (0xb2e1aa) to the cave.  Assembling this reproduces the bytes in
# pm/g7/halo/game_dongle_log exactly.  See docs/dongle-recording.md.
#
# Unlike twd-cave.S and bbhd-cave.S this one writes the input and the output
# with nothing between them, so split-records.py needs --status-bytes 0 (the
# default) and --key-size 32 for it.
    .code64
    .set WRITESLOT, 0x1691cb0       # the game's write import slot
    .set HDEC, 0xb2e1aa             # the real hasp_decrypt
    .globl _start
# called as hasp_decrypt(handle=edi, buf=rsi, len=edx)
_start:
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15            # 5 pushes: rsp 16-aligned
    sub $16,%rsp
    mov %edi,%r12d       # handle
    mov %rsi,%r13        # buf
    mov %edx,%r14d       # len
    mov %r14d,(%rsp)     # len for writing
    # tag
    mov $2,%edi
    lea tag(%rip),%rsi
    mov $4,%edx
    mov WRITESLOT,%rax
    call *%rax
    mov $2,%edi
    mov %rsp,%rsi
    mov $4,%edx
    mov WRITESLOT,%rax
    call *%rax
    mov $2,%edi
    mov %r13,%rsi
    mov %r14d,%edx
    mov WRITESLOT,%rax
    call *%rax
    # real call
    mov %r12d,%edi
    mov %r13,%rsi
    mov %r14d,%edx
    mov $HDEC,%rax
    call *%rax
    mov %eax,%r15d
    mov $2,%edi
    mov %r13,%rsi
    mov %r14d,%edx
    mov WRITESLOT,%rax
    call *%rax
    mov %r15d,%eax
    add $16,%rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    ret
tag: .ascii "@@D@"
