/* MotoGP (32-bit, g6): log the dongle's encrypt and decrypt calls.
 *
 * Same generic stub as bbhd-cave.S, retargeted at MotoGP's addresses
 * (TeknoParrot's "game", which BudgieLoader runs).
 *
 *   as --32 mgp-cave.S -o mgp-cave.o
 *   ld -m elf_i386 -Ttext=0x8cab400 -e _start -o mgp-cave.elf mgp-cave.o
 *   objcopy -O binary -j .text mgp-cave.elf mgp-cave.bin     # 0x98 bytes
 *
 * written at file offset 0xc63400 (VMA 0x8cab400, an unreferenced zero run at
 * the tail of the R E segment), with the two calls repointed:
 *   0x83e301e  hasp_encrypt 0x85878ac -> 0x8cab400
 *   0x83e31a1  hasp_decrypt 0x8587998 -> 0x8cab40c
 * and the anti-debug guard (0x84884fa, it forks) made "mov $1,%eax; ret".
 *
 * Writes the call's 4-byte status between input and output, so
 * split-records.py needs --status-bytes 4.
 */
    .code32
    .set WRITE,   0x8056970         /* the game's write@plt          */
    .set HENC,    0x85878ac         /* the real hasp_encrypt         */
    .set HDEC,    0x8587998         /* the real hasp_decrypt         */
    .set CAVE,    0x8cab400         /* where this is injected        */
    .set TAG_ENC, CAVE + 0x90
    .set TAG_DEC, CAVE + 0x94
    .globl _start

/* hasp_encrypt(handle, buf, len) */
_start:
    push $HENC
    push $TAG_ENC
    jmp  body

/* hasp_decrypt(handle, buf, len) */
    push $HDEC
    push $TAG_DEC
    jmp  body

/* On entry the trampoline has pushed the marker and the real function, so
 *   4(%ebp) marker   8(%ebp) real function   0xc(%ebp) return address
 *   0x10(%ebp) handle   0x14(%ebp) buf   0x18(%ebp) len
 */
body:
    push %ebp
    mov  %esp,%ebp
    push %ebx
    push %esi
    push %edi
    sub  $8,%esp

    push $4                         /* write(2, marker, 4)           */
    push 0x4(%ebp)
    push $2
    call WRITE
    add  $0xc,%esp

    lea  0x18(%ebp),%eax            /* write(2, &len, 4)             */
    push $4
    push %eax
    push $2
    call WRITE
    add  $0xc,%esp

    push 0x18(%ebp)                 /* write(2, buf, len) -- input   */
    push 0x14(%ebp)
    push $2
    call WRITE
    add  $0xc,%esp

    push 0x18(%ebp)                 /* the real call, in place       */
    push 0x14(%ebp)
    push 0x10(%ebp)
    call *0x8(%ebp)
    add  $0xc,%esp
    mov  %eax,-0x10(%ebp)           /* keep its status               */

    lea  -0x10(%ebp),%eax           /* write(2, &status, 4)          */
    push $4
    push %eax
    push $2
    call WRITE
    add  $0xc,%esp

    push 0x18(%ebp)                 /* write(2, buf, len) -- output  */
    push 0x14(%ebp)
    push $2
    call WRITE
    add  $0xc,%esp

    mov  -0x10(%ebp),%eax           /* return the real status        */
    add  $8,%esp
    pop  %edi
    pop  %esi
    pop  %ebx
    pop  %ebp
    add  $8,%esp                    /* drop the trampoline's pushes  */
    ret

    .org 0x90
tag_enc:
    .ascii "@@E@"
tag_dec:
    .ascii "@@D@"
