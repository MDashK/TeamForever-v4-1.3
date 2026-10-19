# Runs an .xex in this project's Xenia Canary for a while, optionally presses controller buttons through Xenia's
# keyboard emulation and takes screenshots, then closes Xenia. Used to check the port without manual interaction.
#
#   powershell -File xenia_run.ps1 -Xex <path\default.xex> [-Seconds 60] [-Screenshot <png path>]
#                                  [-Keys "8:X,12:A,..."] [-Shots "10,20,..."]
#
# -Keys: "<seconds after launch>:<button>[:<hold ms>]" entries. Xenia then runs with --keyboard_mode=1, which maps
#        keys to controller 0 (see [HID.Key] in xenia-canary.config.toml). Buttons: START, A, B, X, Y, BACK, LB, RB,
#        UP, DOWN, LEFT, RIGHT (the left stick, Xenia's "_W/_A/_S/_D" bindings), or a raw virtual-key code (0x..).
# -Shots: extra screenshots at these times (seconds after launch), saved next to -Screenshot as <name>_<t>.png.
#         -Screenshot itself is taken at the end.
param(
	[Parameter(Mandatory = $true)][string]$Xex,
	[int]$Seconds = 60,
	[string]$Screenshot = "",
	[string]$Keys = "",
	[string]$Shots = "",
	[string]$XeniaDir = ""		# Default: $env:XeniaDir, else xenia_canary_windows\ in the work folder (see paths.ps1)
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32Window
{
	[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
	[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
	[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
	[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
	[DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
}
"@

# Xenia's default [HID.Key] bindings
$buttonKeys = @{
	"START" = 0x58; "BACK" = 0x5A; "A" = 0xBA; "B" = 0xDE; "X" = 0x4C; "Y" = 0x50; "LB" = 0x31; "RB" = 0x33;
	"UP" = 0x57; "LEFT" = 0x41; "DOWN" = 0x53; "RIGHT" = 0x44
}

function Save-Screenshot($process, $path) {
	$process.Refresh()
	$handle = $process.MainWindowHandle
	if ($handle -eq [IntPtr]::Zero) { "No Xenia window found for the screenshot"; return }
	$rect = New-Object Win32Window+RECT
	[Win32Window]::GetWindowRect($handle, [ref]$rect) | Out-Null
	$width = $rect.Right - $rect.Left
	$height = $rect.Bottom - $rect.Top
	$bitmap = New-Object System.Drawing.Bitmap $width, $height
	$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
	# PrintWindow with PW_RENDERFULLCONTENT (2) also captures D3D12 content when the desktop can't be read with CopyFromScreen
	$hdc = $graphics.GetHdc()
	[Win32Window]::PrintWindow($handle, $hdc, 2) | Out-Null
	$graphics.ReleaseHdc($hdc)
	$bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
	$graphics.Dispose()
	$bitmap.Dispose()
	"Screenshot saved: $path ($width x $height)"
}

function Press-Key($process, [int]$vk, [int]$holdMs) {
	$process.Refresh()
	if ($process.MainWindowHandle -ne [IntPtr]::Zero) { [Win32Window]::SetForegroundWindow($process.MainWindowHandle) | Out-Null }
	Start-Sleep -Milliseconds 50
	$scan = [byte][Win32Window]::MapVirtualKey([uint32]$vk, 0)
	[Win32Window]::keybd_event([byte]$vk, $scan, 0, [UIntPtr]::Zero)
	Start-Sleep -Milliseconds $holdMs
	[Win32Window]::keybd_event([byte]$vk, $scan, 2, [UIntPtr]::Zero)
}

# The timeline: key presses and screenshots, sorted by time
$events = @()
if ($Keys -ne "") {
	foreach ($entry in $Keys.Split(",")) {
		$parts = $entry.Trim().Split(":")
		$name = $parts[1].ToUpper()
		$vk = if ($buttonKeys.ContainsKey($name)) { $buttonKeys[$name] } else { [Convert]::ToInt32($parts[1], 16) }
		$hold = if ($parts.Count -ge 3) { [int]$parts[2] } else { 150 }
		$events += [pscustomobject]@{ Time = [double]$parts[0]; Kind = "key"; Vk = $vk; Hold = $hold; Name = $name }
	}
}
if ($Shots -ne "" -and $Screenshot -ne "") {
	foreach ($t in $Shots.Split(",")) {
		$base = [System.IO.Path]::ChangeExtension($Screenshot, $null).TrimEnd(".")
		$events += [pscustomobject]@{ Time = [double]$t.Trim(); Kind = "shot"; Path = "$($base)_$($t.Trim()).png" }
	}
}
$events = $events | Sort-Object Time

. (Join-Path $PSScriptRoot "paths.ps1")
$xeniaDir = if ($XeniaDir -ne "") { $XeniaDir } elseif ($env:XeniaDir) { $env:XeniaDir } else { Join-Path (Resolve-WorkDir "") "xenia_canary_windows" }
$arguments = @()
if ($Keys -ne "") { $arguments += "--keyboard_mode=1" }
$arguments += "`"$Xex`""
$process = Start-Process -FilePath (Join-Path $xeniaDir "xenia_canary.exe") -ArgumentList $arguments -WorkingDirectory $xeniaDir -PassThru
$start = Get-Date

foreach ($event in $events) {
	$wait = $event.Time - ((Get-Date) - $start).TotalSeconds
	if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]($wait * 1000)) }
	if ($process.HasExited) { break }
	if ($event.Kind -eq "key") {
		Press-Key $process $event.Vk $event.Hold
		"[{0,5:N1} s] pressed {1}" -f ((Get-Date) - $start).TotalSeconds, $event.Name
	} else {
		Save-Screenshot $process $event.Path
	}
}

$remaining = $Seconds - ((Get-Date) - $start).TotalSeconds
if ($remaining -gt 0) { Start-Sleep -Milliseconds ([int]($remaining * 1000)) }

if ($Screenshot -ne "" -and -not $process.HasExited) {
	Save-Screenshot $process $Screenshot
}

$exited = $process.HasExited
if (-not $exited) { $process | Stop-Process -Force }
"Xenia exited by itself: $exited"
