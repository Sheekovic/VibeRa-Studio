param([string]$Project=(Join-Path $PSScriptRoot '../dist/VibeRa Studio/Examples/Midnight Session.vibra'), [string]$Output=(Join-Path $PSScriptRoot '../build/navigation-floating-native'), [switch]$UseGlobalInput)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force -Path $Output | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class VibeRaFloatingObservation {
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
 [StructLayout(LayoutKind.Sequential)] public struct Point { public int x,y; }
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref Point p);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point p);
 [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point p);
 [DllImport("user32.dll")] public static extern void mouse_event(uint flags,uint dx,uint dy,uint data,UIntPtr extra);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
}
'@
[VibeRaFloatingObservation]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
$exe=Join-Path $root 'dist/VibeRa Studio/VibeRa Studio.exe'
$app=Start-Process -FilePath $exe -ArgumentList @('--window-check-project',('"'+$Project+'"')) -WindowStyle Normal -PassThru
$ownedHandle=$app.Handle
$creation=$app.StartTime.ToUniversalTime().ToString('o')
$record=[System.Collections.Generic.List[string]]::new()
$cursor=New-Object VibeRaFloatingObservation+Point
[VibeRaFloatingObservation]::GetCursorPos([ref]$cursor) | Out-Null
$script:ownedPointerDown=$false
try {
 for($i=0;$i -lt 40;$i++) { Start-Sleep -Milliseconds 150; $app.Refresh(); if($app.MainWindowHandle -ne [IntPtr]::Zero) { break } }
 $h=$app.MainWindowHandle
 if($h -eq [IntPtr]::Zero) { throw 'Owned window missing' }
 function Assert-Owned {
  $pidCheck=[uint32]0; [VibeRaFloatingObservation]::GetWindowThreadProcessId($h,[ref]$pidCheck) | Out-Null
  if($pidCheck -ne $app.Id -or $app.HasExited) { throw 'Owned window identity lost' }
 }
 function Capture([string]$name) {
  Assert-Owned; Start-Sleep -Milliseconds 300
  $r=New-Object VibeRaFloatingObservation+Rect; [VibeRaFloatingObservation]::GetClientRect($h,[ref]$r) | Out-Null
  $bitmap=New-Object System.Drawing.Bitmap($r.right,$r.bottom)
  $graphics=[System.Drawing.Graphics]::FromImage($bitmap); $dc=$graphics.GetHdc()
  try { if(-not [VibeRaFloatingObservation]::PrintWindow($h,$dc,3)) { throw 'Native capture failed' } }
  finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
  try { $bitmap.Save((Join-Path $Output $name),[System.Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
 }
 function Pointer([uint32]$message,[int]$flags,[double]$x,[double]$y) {
  Assert-Owned
  if(-not $script:ownedPointerDown -and ([VibeRaFloatingObservation]::GetAsyncKeyState(1) -band 32768) -ne 0) { throw 'Mouse is active; test paused' }
  $px=[int]($x*$scale); $py=[int](($y+36)*$scale)
  if($UseGlobalInput) {
   if([VibeRaFloatingObservation]::GetForegroundWindow() -ne $h) { throw 'Foreground ownership lost; no further input' }
   $origin=New-Object VibeRaFloatingObservation+Point; [VibeRaFloatingObservation]::ClientToScreen($h,[ref]$origin) | Out-Null
   [VibeRaFloatingObservation]::SetCursorPos(($origin.x+$px),($origin.y+$py)) | Out-Null
   if($message -eq 513) { [VibeRaFloatingObservation]::mouse_event(2,0,0,0,[UIntPtr]::Zero); $script:ownedPointerDown=$true }
   if($message -eq 514) { [VibeRaFloatingObservation]::mouse_event(4,0,0,0,[UIntPtr]::Zero); $script:ownedPointerDown=$false }
   Start-Sleep -Milliseconds 35
   return
  }
  $packed=($py -shl 16) -bor ($px -band 65535)
  [VibeRaFloatingObservation]::SendMessage($h,$message,[IntPtr]$flags,[IntPtr]$packed) | Out-Null
 }
 function Drag([double]$x,[double]$y,[double]$dx,[double]$dy) {
  Pointer 512 0 $x $y; Pointer 513 1 $x $y
  for($i=1;$i -le 8;$i++) { Pointer 512 1 ($x+$dx*$i/8) ($y+$dy*$i/8); Start-Sleep -Milliseconds 20 }
  Pointer 514 0 ($x+$dx) ($y+$dy)
 }
 Assert-Owned
 [VibeRaFloatingObservation]::SetWindowPos($h,[IntPtr]::Zero,120,100,1920,1080,16) | Out-Null
 Start-Sleep -Milliseconds 350
 if($UseGlobalInput) {
  [VibeRaFloatingObservation]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 150
  if([VibeRaFloatingObservation]::GetForegroundWindow() -ne $h) {
   if(([VibeRaFloatingObservation]::GetAsyncKeyState(1) -band 32768) -ne 0) { throw 'Mouse active; no activation input sent' }
   $origin=New-Object VibeRaFloatingObservation+Point; [VibeRaFloatingObservation]::ClientToScreen($h,[ref]$origin) | Out-Null
   $caption=New-Object VibeRaFloatingObservation+Point
   $caption.x=$origin.x+[int](300*([VibeRaFloatingObservation]::GetDpiForWindow($h)/96.0)); $caption.y=$origin.y+[int](18*([VibeRaFloatingObservation]::GetDpiForWindow($h)/96.0))
   $under=[VibeRaFloatingObservation]::WindowFromPoint($caption); $underPid=[uint32]0
   [VibeRaFloatingObservation]::GetWindowThreadProcessId($under,[ref]$underPid) | Out-Null
   if($under -ne $h -or $underPid -ne $app.Id) { throw 'Owned caption obscured; no activation input sent' }
   [VibeRaFloatingObservation]::SetCursorPos($caption.x,$caption.y) | Out-Null
   [VibeRaFloatingObservation]::mouse_event(2,0,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 40
   [VibeRaFloatingObservation]::mouse_event(4,0,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 120
   $record.Add('Foreground activation used a verified owned-caption click.')
  }
  if([VibeRaFloatingObservation]::GetForegroundWindow() -ne $h) { throw 'Cannot acquire foreground; no mouse input sent' }
 }
 $client=New-Object VibeRaFloatingObservation+Rect; [VibeRaFloatingObservation]::GetClientRect($h,[ref]$client) | Out-Null
 $scale=[VibeRaFloatingObservation]::GetDpiForWindow($h)/96.0
 $width=$client.right/$scale; $height=$client.bottom/$scale-36
 $mixer=if($height -lt 800){32}else{194}
 $rackTop=84+[Math]::Floor(($height-113-$mixer-7-7)*.52)+7
 $rackLeft=$width-4-450
 $mainBefore=New-Object VibeRaFloatingObservation+Rect; [VibeRaFloatingObservation]::GetWindowRect($h,[ref]$mainBefore) | Out-Null
 Capture 'native-floating-before.png'
 Drag ($rackLeft+100) ($rackTop+15) -80 -70
 Capture 'native-rack-floating.png'
 $rackHeight=$height-29-$mixer-7-$rackTop
 # Stay inside the four-logical-pixel border despite fractional DPI rounding.
 Drag ($rackLeft-80+446.5) ($rackTop-70+$rackHeight/2) 30 0
 Capture 'native-rack-resized.png'
 $mainAfter=New-Object VibeRaFloatingObservation+Rect; [VibeRaFloatingObservation]::GetWindowRect($h,[ref]$mainAfter) | Out-Null
 if($mainBefore.left -ne $mainAfter.left -or $mainBefore.top -ne $mainAfter.top -or $mainBefore.right -ne $mainAfter.right -or $mainBefore.bottom -ne $mainAfter.bottom) { throw 'Main window moved instead of internal panel' }
 $before=(Get-FileHash -LiteralPath (Join-Path $Output 'native-floating-before.png')).Hash
 $moved=(Get-FileHash -LiteralPath (Join-Path $Output 'native-rack-floating.png')).Hash
 $resized=(Get-FileHash -LiteralPath (Join-Path $Output 'native-rack-resized.png')).Hash
 if($before -eq $moved -or $moved -eq $resized) { throw 'Native drag/resize did not both produce rendered changes; inspect captures' }
 $record.Add("Owned PID $($app.Id), creation $creation, SHA256 $((Get-FileHash -LiteralPath $exe).Hash), DPI $($scale*96)")
 $record.Add("PASS: native drag/resize and distinct live captures; OS synthetic input=$UseGlobalInput; main window bounds retained. Images require visual inspection. No physical gestures, hardware or listening.")
} catch { $record.Add("NOT COMPLETED: $($_.Exception.Message)"); throw }
finally {
 if($script:ownedPointerDown) { [VibeRaFloatingObservation]::mouse_event(4,0,0,0,[UIntPtr]::Zero) }
 if($UseGlobalInput -and [VibeRaFloatingObservation]::GetForegroundWindow() -eq $h) { [VibeRaFloatingObservation]::SetCursorPos($cursor.x,$cursor.y) | Out-Null }
 $record | Set-Content -LiteralPath (Join-Path $Output 'native-floating-verification.txt') -Encoding UTF8
 if(-not $app.HasExited) { $null=$app.CloseMainWindow(); if(-not $app.WaitForExit(3000)) { $app.Kill(); $app.WaitForExit() } }
}
$record
