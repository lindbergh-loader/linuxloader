/* The Walking Dead (32-bit): log the derived file key.
 *
 * Recovered by disassembling pm/game_keydump, the only surviving record of
 * it.  Assembling at CAVE reproduces that copy's bytes exactly.
 *
 *   as --32 twd-keydump.S -o twd-keydump.o
 *   ld -m elf_i386 -Ttext=0x8a25800 -e _start -o twd-keydump.elf twd-keydump.o
 *   objcopy -O binary -j .text twd-keydump.elf twd-keydump.bin   # 0x6b bytes
 *
 * written at file offset 0x9dd800, reached by overwriting the 8 bytes at
 * 0x80e2310 ("mov %ebx,(%esp); call 0x8170a20") with a jmp here plus three
 * nops.  This is a detour rather than a wrapper: it logs, re-executes the
 * instructions it displaced, and jumps back.
 */
    .code32
    .set WRITE, 0x8055670           /* the game's write@plt              */
    .set CALLEE, 0x8170a20          /* the call that was displaced       */
    .set RETURN, 0x80e2318          /* just past the patched site        */
    .set CAVE,  0x8a25800
    .set TAG,   CAVE + 0x60
    .globl _start
_start:
    pusha
    push $0xb                       /* write(2, tag, 11)                 */
    push $TAG
    push $2
    call WRITE
    add  $0xc,%esp
    lea  0x63(%esp),%eax            /* the key, past pusha's 32 bytes    */
    push $0x30                      /* write(2, key, 48)                 */
    push %eax
    push $2
    call WRITE
    add  $0xc,%esp
    popa
    mov  %ebx,(%esp)                /* displaced                         */
    call CALLEE                     /* displaced                         */
    jmp  RETURN
    .org 0x60
tag:
    .ascii "\n@@TWDKEY@@"
