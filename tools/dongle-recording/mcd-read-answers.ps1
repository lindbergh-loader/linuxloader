$cs=@"
using System;
using System.Runtime.InteropServices;
public class Mem {
 [DllImport("kernel32.dll",SetLastError=true)] public static extern IntPtr OpenProcess(uint a,bool i,int pid);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr h,IntPtr b,byte[] buf,int sz,out int rd);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
 [DllImport("advapi32.dll",SetLastError=true)] public static extern bool OpenProcessToken(IntPtr p,uint a,out IntPtr t);
 [DllImport("kernel32.dll")] public static extern IntPtr GetCurrentProcess();
 [DllImport("advapi32.dll",SetLastError=true)] public static extern bool LookupPrivilegeValue(string s,string n,out long l);
 [StructLayout(LayoutKind.Sequential)] public struct TP{public int Count;public long Luid;public int Attr;}
 [DllImport("advapi32.dll",SetLastError=true)] public static extern bool AdjustTokenPrivileges(IntPtr t,bool d,ref TP n,int l,IntPtr p,IntPtr r);
 public static void Dbg(){IntPtr t;OpenProcessToken(GetCurrentProcess(),0x28,out t);long l;LookupPrivilegeValue(null,"SeDebugPrivilege",out l);TP tp=new TP();tp.Count=1;tp.Luid=l;tp.Attr=2;AdjustTokenPrivileges(t,false,ref tp,0,IntPtr.Zero,IntPtr.Zero);}
 public static byte[] Read(int pid,long addr,int size){Dbg();IntPtr h=OpenProcess(0x410,false,pid);if(h==IntPtr.Zero)return null;byte[] b=new byte[size];int r;bool ok=ReadProcessMemory(h,(IntPtr)addr,b,size,out r);CloseHandle(h);if(!ok||r!=size)return null;return b;}
}
"@
Add-Type $cs
function hx($b){ if($b -eq $null){return "NULL"}; ($b|%{$_.ToString("x2")}) -join "" }
$found=$false
foreach($p in Get-Process BudgieLoader_x64,gamedump -ErrorAction SilentlyContinue){
 $m=[Mem]::Read($p.Id,0x400000,4)
 Write-Output ("pid="+$p.Id+" base@0x400000="+(hx $m))
 if((hx $m) -eq "7f454c46"){
   $found=$true
   $b128=[Mem]::Read($p.Id,0x16dee00,128); $b64=[Mem]::Read($p.Id,0x16dedc0,64)
   Write-Output ("PLAIN128="+(hx $b128))
   Write-Output ("PLAIN64="+(hx $b64))
   if($b128 -ne $null){[IO.File]::WriteAllBytes("C:\mcd\plain128.bin",$b128)}
   if($b64 -ne $null){[IO.File]::WriteAllBytes("C:\mcd\plain64.bin",$b64)}
 }
}
if(-not $found){ Write-Output "NO-ELF-PROCESS (is the game running?)" }
