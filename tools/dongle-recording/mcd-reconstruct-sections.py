#!/usr/bin/env python3
# Rebuild ELF section headers for the Minecraft Dungeons Arcade memory dump
# (section-less; BudgieLoader/ElfLdr2 needs SHT_DYNSYM to resolve imports).
# Values below are read from the dump's PT_DYNAMIC (readelf -d gamedump.exec).
#   usage: mcd-reconstruct-sections.py <in dump> <out dump>
import sys,struct,zlib
BASE=0x400000
src,dst=sys.argv[1],sys.argv[2]
d=bytearray(open(src,'rb').read()); N0=len(d)
def off(vma): return vma-BASE
SHT_HASH,SHT_STRTAB,SHT_RELA,SHT_DYNAMIC,SHT_DYNSYM=5,3,4,6,11
nbucket,nchain=139,214
secs=[
 ("",0,0,0,0,0,0,0,0),
 (".hash",SHT_HASH,2,0x1fa1758,(nbucket+nchain+2)*4,2,0,8,4),
 (".dynsym",SHT_DYNSYM,2,0x400458,nchain*24,3,1,8,24),
 (".dynstr",SHT_STRTAB,2,0x4044f0,3131,0,0,1,0),
 (".rela.dyn",SHT_RELA,2,0x408030,384,2,0,8,24),
 (".rela.plt",SHT_RELA,0x42,0x408378,4296,2,0,8,24),
 (".dynamic",SHT_DYNAMIC,3,0x16d27a8,0x290,3,0,8,16),
 (".shstrtab",SHT_STRTAB,0,0,0,0,0,1,0),
]
names=b"\x00"; nameoff={}
for s in secs:
    nameoff[s[0]]=len(names) if s[0] else 0
    if s[0]: names+=s[0].encode()+b"\x00"
pad8=lambda x:(x+7)&~7
shstr_off=pad8(N0); d+=b"\x00"*(shstr_off-N0); d+=names
shtab_off=pad8(len(d)); d+=b"\x00"*(shtab_off-len(d))
for i,(name,typ,flags,addr,size,link,info,align,ent) in enumerate(secs):
    sh_off = shstr_off if name==".shstrtab" else (0 if typ==0 else off(addr))
    sz = len(names) if name==".shstrtab" else size
    d+=struct.pack('<IIQQQQIIQQ',nameoff[name],typ,flags,addr,sh_off,sz,link,info,align,ent)
struct.pack_into('<Q',d,0x28,shtab_off); struct.pack_into('<H',d,0x3a,64)
struct.pack_into('<H',d,0x3c,len(secs)); struct.pack_into('<H',d,0x3e,7)
open(dst,'wb').write(d); import os; os.chmod(dst,0o755)
print("wrote %s crc=0x%08x shoff=0x%x"%(dst,zlib.crc32(d)&0xffffffff,shtab_off))
