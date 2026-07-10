# Drives a synthetic mouse drag through the running Min3D game window to
# verify click-and-drag route building end to end.
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class MinInput {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, UIntPtr dwExtraInfo);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    public const uint LEFTDOWN = 0x0002;
    public const uint LEFTUP = 0x0004;
    public const uint KEYUP = 0x0002;
}
"@

$proc = Get-Process UnrealEditor -ErrorAction Stop | Where-Object { $_.MainWindowTitle -ne "" } | Select-Object -First 1
Write-Host "Window: '$($proc.MainWindowTitle)' pid=$($proc.Id)"
[MinInput]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 800

$rect = New-Object MinInput+RECT
[MinInput]::GetWindowRect($proc.MainWindowHandle, [ref]$rect) | Out-Null
$cx = [int](($rect.Left + $rect.Right) / 2)
$cy = [int](($rect.Top + $rect.Bottom) / 2)
Write-Host "Window rect $($rect.Left),$($rect.Top) - $($rect.Right),$($rect.Bottom) center $cx,$cy"

# select the Tunnel tool (key 1)
[MinInput]::keybd_event(0x31, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 60
[MinInput]::keybd_event(0x31, 0, [MinInput]::KEYUP, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 400

# drag from left-of-center to right-of-center
$x0 = $cx - 220; $y0 = $cy + 60
[MinInput]::SetCursorPos($x0, $y0) | Out-Null
Start-Sleep -Milliseconds 300
[MinInput]::mouse_event([MinInput]::LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 150
for ($i = 1; $i -le 22; $i++) {
    [MinInput]::SetCursorPos($x0 + $i * 20, $y0) | Out-Null
    Start-Sleep -Milliseconds 40
}
Start-Sleep -Milliseconds 200
[MinInput]::mouse_event([MinInput]::LEFTUP, 0, 0, 0, [UIntPtr]::Zero)
Write-Host "Drag done"
Start-Sleep -Milliseconds 800

# take an in-game screenshot with UI (key F12 not bound; use console-free approach: skip)
