/* Big Buck HD Wild (32-bit, g5): log the dongle's encrypt and decrypt calls.
 *
 * Same generic stub as twd-cave.S, retargeted at BBHDW's addresses.
 *
 *   as --32 bbhd-cave.S -o bbhd-cave.o
 *   ld -m elf_i386 -Ttext=0x8afb422 -e _start -o bbhd-cave.elf bbhd-cave.o
 *   objcopy -O binary -j .text bbhd-cave.elf bbhd-cave.bin     # 0x98 bytes
 *
 * written at file offset 0xab3422 (VMA 0x8afb422, an unreferenced zero run in
 * .rodata, which sits in the first R E segment so it is executable), with the
 * two calls repointed:
 *   0x8266cfc  hasp_encrypt 0x853004c -> 0x8afb422
 *   0x8266c8f  hasp_decrypt 0x8530138 -> 0x8afb42e
 *
 * Writes the call's 4-byte status between input and output, so
 * split-records.py needs --status-bytes 4.
 */
    .code32
    .set WRITE,   0x804f828         /* the game's write@plt          */
    .set HENC,    0x853004c         /* the real hasp_encrypt         */
    .set HDEC,    0x8530138         /* the real hasp_decrypt         */
    .set CAVE,    0x8afb422         /* where this is injected        */
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
