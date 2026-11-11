# Captures the main window of a running process (by id) at a chosen client size,
# using PrintWindow(PW_RENDERFULLCONTENT) so the window need not be in front.
# The size is in physical pixels; pass logical size x DPI scale.
param(
    [Parameter(Mandatory)][int]$ProcessId,
    [Parameter(Mandatory)][string]$Output,
    [int]$Width = 0,
    [int]$Height = 0
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ReviewWin {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool repaint);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[ReviewWin]::SetProcessDPIAware() | Out-Null
$process = Get-Process -Id $ProcessId
$handle = $process.MainWindowHandle
if ($handle -eq [IntPtr]::Zero) { throw "Process $ProcessId has no main window." }
$rect = New-Object ReviewWin+RECT
[ReviewWin]::GetWindowRect($handle, [ref]$rect) | Out-Null
if ($Width -gt 0 -and $Height -gt 0) {
    [ReviewWin]::MoveWindow($handle, 20, 20, $Width, $Height, $true) | Out-Null
    Start-Sleep -Milliseconds 1200
    [ReviewWin]::GetWindowRect($handle, [ref]$rect) | Out-Null
}
$w = $rect.R - $rect.L; $h = $rect.B - $rect.T
$bitmap = New-Object System.Drawing.Bitmap $w, $h
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$dc = $graphics.GetHdc()
[ReviewWin]::PrintWindow($handle, $dc, 2) | Out-Null
$graphics.ReleaseHdc($dc)
$bitmap.Save($Output, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose(); $bitmap.Dispose()
"$Output ${w}x${h}"
