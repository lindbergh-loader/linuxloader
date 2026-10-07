/* The Walking Dead (32-bit): log the gun aim the engine hands out.
 *
 * Recovered by disassembling pm/game_aim_log, the only surviving record of
 * it.  Assembling at CAVE reproduces that copy's bytes exactly.
 *
 *   as --32 twd-aim.S -o twd-aim.o
 *   ld -m elf_i386 -Ttext=0x8a25a00 -e _start -o twd-aim.elf twd-aim.o
 *   objcopy -O binary -j .text twd-aim.elf twd-aim.bin           # 0x33 bytes
 *
 * written at file offset 0x9dda00, reached by overwriting the 6 bytes at
 * 0x8419768 ("push %ebp; mov %esp,%ebp; mov 0x8(%ebp),%edx") with a jmp here
 * plus one nop.  Same detour shape as twd-keydump.S; nothing to do with the
 * dongle, kept because it is the same technique pointed at another call.
 */
    .code32
    .set WRITE,  0x8055670          /* the game's write@plt              */
    .set RETURN, 0x841976e          /* just past the patched site        */
    .set CAVE,   0x8a25a00
    .set TAG,    CAVE + 0x2f
    .globl _start
_start:
    pusha
    push $0x4                       /* write(2, "@@A@", 4)               */
    push $TAG
    push $2
    call WRITE
    add  $0xc,%esp
    lea  0x24(%esp),%eax            /* the aim, on the stack             */
    push $0xc                       /* write(2, aim, 12)                 */
    push %eax
    push $2
    call WRITE
    add  $0xc,%esp
    popa
    push %ebp                       /* displaced                         */
    mov  %esp,%ebp                  /* displaced                         */
    mov  0x8(%ebp),%edx             /* displaced                         */
    jmp  RETURN
    .org 0x2f
tag:
    .ascii "@@A@"
