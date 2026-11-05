# Generic HASP-answer reader: dump fixed addresses from a running TeknoParrot
# ELF game process. See docs/dongle-answers-via-windows-memory.md.
#   read-process-mem.ps1 -Proc BudgieLoader_x64 -Reads "0x16dee00:128,0x16dedc0:64" -OutDir C:\out
param(
  [string]$Proc = "BudgieLoader_x64",
  [Parameter(Mandatory=$true)][string]$Reads,   # "addr:len,addr:len" (addr hex or dec)
  [string]$OutDir = "C:\out",
  [long]$ElfBase = 0x400000
)
$cs=@"
using System;using System.Runtime.InteropServices;
public class Mem{
 [DllImport("kernel32.dll",SetLastError=true)] public static extern IntPtr OpenProcess(uint a,bool i,int p);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr h,IntPtr b,byte[] buf,int s,out int r);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
 [DllImport("advapi32.dll",SetLastError=true)] public static extern bool OpenProcessToken(IntPtr p,uint a,out IntPtr t);
 [DllImport("kernel32.dll")] public static extern IntPtr GetCurrentProcess();
 [DllImport("advapi32.dll",SetLastError=true)] public static extern bool LookupPrivilegeValue(string s,string n,out long l);
 [StructLayout(LayoutKind.Sequential)] public struct TP{public int C;public long L;public int A;}
 [DllImport("advapi32.dll",SetLastError=true)] public static extern bool AdjustTokenPrivileges(IntPtr t,bool d,ref TP n,int l,IntPtr p,IntPtr r);
 public static void Dbg(){IntPtr t;OpenProcessToken(GetCurrentProcess(),0x28,out t);long l;LookupPrivilegeValue(null,"SeDebugPrivilege",out l);TP tp=new TP();tp.C=1;tp.L=l;tp.A=2;AdjustTokenPrivileges(t,false,ref tp,0,IntPtr.Zero,IntPtr.Zero);}
 public static byte[] Read(int pid,long a,int s){Dbg();IntPtr h=OpenProcess(0x410,false,pid);if(h==IntPtr.Zero)return null;byte[] b=new byte[s];int r;bool ok=ReadProcessMemory(h,(IntPtr)a,b,s,out r);CloseHandle(h);if(!ok||r!=s)return null;return b;}
}
"@
Add-Type $cs
function hx($b){ if($b -eq $null){return "NULL"}; ($b|%{$_.ToString("x2")}) -join "" }
if(-not (Test-Path $OutDir)){ New-Item -ItemType Directory -Force $OutDir | Out-Null }
$target=$null
foreach($p in Get-Process $Proc -ErrorAction SilentlyContinue){
  $m=[Mem]::Read($p.Id,$ElfBase,4)
  if((hx $m) -eq "7f454c46"){ $target=$p.Id; Write-Output ("process pid="+$p.Id+" ELF@0x"+$ElfBase.ToString("x")) ; break }
}
if($target -eq $null){ Write-Output "No ELF process found (is the game running?)"; exit 1 }
foreach($pair in $Reads.Split(",")){
  $a=$pair.Split(":")[0]; $len=[int]$pair.Split(":")[1]
  $addr = if($a.StartsWith("0x")){[Convert]::ToInt64($a,16)}else{[int64]$a}
  $b=[Mem]::Read($target,$addr,$len)
  Write-Output ("0x"+$addr.ToString("x")+" len="+$len+" = "+(hx $b))
  if($b -ne $null){ $f=Join-Path $OutDir ("mem_0x"+$addr.ToString("x")+"_len"+$len+".bin"); [IO.File]::WriteAllBytes($f,$b) }
}
