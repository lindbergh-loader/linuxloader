/* Ghostbusters Arcade v01.17 (32-bit, g6): record HASP answers under
 * TeknoParrot's BudgieLoader. Inject into the zero run at file offset
 * 0x9370a2 (VMA 0x897f0a2), repointing the two calls below. The ELF load
 * segment starts at file offset 0 / VMA 0x8048000, so file offsets of the
 * call instructions are 0x15374f and 0x1537bc.
 *
 *   as --32 gb-cave.S -o /tmp/gb-cave.o
 *   ld -m elf_i386 -Ttext=0x897f0a2 -e _start -o /tmp/gb-cave.elf /tmp/gb-cave.o
 *   objcopy -O binary -j .text /tmp/gb-cave.elf /tmp/gb-cave.bin
 *
 * The decrypt call is at VMA 0x819b74f; encrypt is at 0x819b7bc. The real
 * wrappers are 0x83e2868 and 0x83e277c. Calls target CAVE and CAVE+0xc.
 * Records contain the 4-byte status between input and output, matching the
 * Walking Dead / Big Buck HD Wild recorder format.
 */
    .code32
    .set WRITE,   0x804e060         /* write@plt */
    .set HDEC,    0x83e2868
    .set HENC,    0x83e277c
    .set CAVE,    0x897f0a2
    .set TAG_DEC, CAVE + 0x90
    .set TAG_ENC, CAVE + 0x94
    .globl _start

/* hasp_decrypt(handle, buf, len) */
_start:
    push $HDEC
    push $TAG_DEC
    jmp body

/* hasp_encrypt(handle, buf, len) */
    push $HENC
    push $TAG_ENC
    jmp body

/* Entry stack after either trampoline:
 * 4 marker, 8 real function, c return address, 10 handle, 14 buffer, 18 len.
 */
body:
    push %ebp
    mov  %esp,%ebp
    push %ebx
    push %esi
    push %edi
    sub  $8,%esp

    push $4
    push 0x4(%ebp)
    push $2
    call WRITE
    add  $0xc,%esp

    lea  0x18(%ebp),%eax
    push $4
    push %eax
    push $2
    call WRITE
    add  $0xc,%esp

    push 0x18(%ebp)
    push 0x14(%ebp)
    push $2
    call WRITE
    add  $0xc,%esp

    push 0x18(%ebp)
    push 0x14(%ebp)
    push 0x10(%ebp)
    call *0x8(%ebp)
    add  $0xc,%esp
    mov  %eax,-0x10(%ebp)

    lea  -0x10(%ebp),%eax
    push $4
    push %eax
    push $2
    call WRITE
    add  $0xc,%esp

    push 0x18(%ebp)
    push 0x14(%ebp)
    push $2
    call WRITE
    add  $0xc,%esp

    mov  -0x10(%ebp),%eax
    add  $8,%esp
    pop  %edi
    pop  %esi
    pop  %ebx
    pop  %ebp
    add  $8,%esp
    ret

    .org 0x90
tag_dec:
    .ascii "@@D@"
tag_enc:
    .ascii "@@E@"
