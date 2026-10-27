/* The Walking Dead (32-bit): log the dongle's encrypt and decrypt calls.
 *
 * Recovered by disassembling pm/game_dongle_log, which is the only surviving
 * record of it -- the original source was not kept.  Assembling this at
 * CAVE reproduces that copy's bytes exactly; see docs/dongle-recording.md.
 *
 *   as --32 twd-cave.S -o twd-cave.o
 *   ld -m elf_i386 -Ttext=0x8a25900 -e _start -o twd-cave.elf twd-cave.o
 *   objcopy -O binary -j .text twd-cave.elf twd-cave.bin     # 0x98 bytes
 *
 * written at file offset 0x9dd900, with the two calls repointed:
 *   0x81d724e  hasp_encrypt 0x837bd4c -> 0x8a25900
 *   0x81d73d1  hasp_decrypt 0x837be38 -> 0x8a2590c
 *
 * Unlike Halo's, this one is generic: each trampoline pushes the real
 * function and its own marker, then falls into a shared body, so one body
 * serves both calls.  Note it writes the call's 4-byte status between the
 * input and the output -- split-records.py needs --status-bytes 4 for it.
 */
    .code32
    .set WRITE,   0x8055670         /* the game's write@plt          */
    .set HENC,    0x837bd4c         /* the real hasp_encrypt         */
    .set HDEC,    0x837be38         /* the real hasp_decrypt         */
    .set CAVE,    0x8a25900         /* where this is injected        */
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
