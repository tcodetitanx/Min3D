# Parks the cursor at a known offset in the running Min3D window with the
# Miner tool selected, so an auto-screenshot can verify the cell highlight
# sits exactly under the cursor.
param([int]$OffX = 200, [int]$OffY = 80)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class MinHover {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern void keybd_event(byte k, byte s, uint f, UIntPtr e);
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
"@
$p = Get-Process | Where-Object { $_.ProcessName -like "Min3D*" -or $_.ProcessName -eq "UnrealEditor" } |
    Where-Object { $_.MainWindowHandle -ne 0 -and $_.MainWindowTitle -like "MIN3D*" } | Select-Object -First 1
if (-not $p) { Write-Host "no game window"; exit 1 }
Write-Host "window '$($p.MainWindowTitle)' proc $($p.ProcessName)"
[MinHover]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 800
[MinHover]::keybd_event(0x33, 0, 0, [UIntPtr]::Zero)   # '3' = Miner tool
Start-Sleep -Milliseconds 80
[MinHover]::keybd_event(0x33, 0, 2, [UIntPtr]::Zero)
$r = New-Object MinHover+RECT
[MinHover]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
$cx = [int](($r.Left + $r.Right) / 2)
$cy = [int](($r.Top + $r.Bottom) / 2)
[MinHover]::SetCursorPos($cx + $OffX, $cy + $OffY) | Out-Null
Write-Host "window $($r.Left),$($r.Top)-$($r.Right),$($r.Bottom); cursor parked at window-center + ($OffX,$OffY)"
