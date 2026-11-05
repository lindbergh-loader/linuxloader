# Minecraft Dungeons Arcade (64-bit g7): log the dongle's encrypt and
# decrypt calls.
#
#   as --64 mcd-cave.S -o mcd-cave.o
#   ld -Ttext=0x14a1200 -e _start -o mcd-cave.elf mcd-cave.o
#   objcopy -O binary -j .text mcd-cave.elf mcd-cave.bin    # 218 bytes
#
# written at file offset 0x10a1200 of a copy of pm/g7/minecraft/gamedump.exec
# (the zero padding at the end of the code segment, which maps at 0x400000
# with no skew), with the three calls repointed:
#   0x652f93, 0x653287  hasp_encrypt 0xb66cbd -> 0x14a1200
#   0x6532c7            hasp_decrypt 0xb66dcf -> 0x14a120f
#
# The generic shape of twd-cave.S in 64-bit: each trampoline loads the real
# function and its marker, then falls into a shared body.  It writes the
# call's 4-byte status between the input and the output, so split-records.py
# needs --status-bytes 4.  See docs/dongle-recording.md.
    .code64
    .set WRITESLOT, 0x16d3b40       # the game's write import slot
    .set HENC, 0xb66cbd             # the real hasp_encrypt
    .set HDEC, 0xb66dcf             # the real hasp_decrypt
    .globl _start

# hasp_encrypt(handle=edi, buf=rsi, len=edx)
_start:
    mov $HENC,%r10d
    lea tag_enc(%rip),%r11
    jmp body

# hasp_decrypt(handle=edi, buf=rsi, len=edx)
    mov $HDEC,%r10d
    lea tag_dec(%rip),%r11

body:
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15            # 6 pushes + return address: sub 24 aligns rsp
    sub $24,%rsp
    mov %edi,%r12d       # handle
    mov %rsi,%r13        # buf
    mov %edx,%r14d       # len
    mov %r10,%rbx        # the real function
    mov %r11,%rbp        # the marker
    mov %r14d,(%rsp)     # len for writing
    # marker
    mov $2,%edi
    mov %rbp,%rsi
    mov $4,%edx
    mov WRITESLOT,%rax
    call *%rax
    # length
    mov $2,%edi
    mov %rsp,%rsi
    mov $4,%edx
    mov WRITESLOT,%rax
    call *%rax
    # input
    mov $2,%edi
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
    mov $2,%edi
    lea 4(%rsp),%rsi
    mov $4,%edx
    mov WRITESLOT,%rax
    call *%rax
    # output
    mov $2,%edi
    mov %r13,%rsi
    mov %r14d,%edx
    mov WRITESLOT,%rax
    call *%rax
    mov %r15d,%eax       # the real status, untouched
    add $24,%rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    ret
tag_enc: .ascii "@@E@"
tag_dec: .ascii "@@D@"
