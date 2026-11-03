param([string]$Output=(Join-Path $PSScriptRoot '../build/windows/chrome_native_observation'), [string]$Project='', [switch]$Detached)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force -Path $Output | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public static class VibeRaWindowObservation {
 public delegate bool EnumCallback(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumCallback cb, IntPtr p);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,System.Text.StringBuilder text,int count);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 public static IntPtr FindOwned(uint pid,string prefix) {
  IntPtr found=IntPtr.Zero;
  EnumWindows((h,p)=>{ uint id; GetWindowThreadProcessId(h,out id);
   if(id==pid) { var text=new System.Text.StringBuilder(256); GetWindowText(h,text,256);
    if(text.ToString().StartsWith(prefix)) { found=h; return false; } } return true; },IntPtr.Zero);
  return found;
 }

 [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h,uint attribute,out int value,int size);
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
 [StructLayout(LayoutKind.Sequential)] public struct Point { public int x,y; }
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref Point p);
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int height,uint flags);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
}
"@
[VibeRaWindowObservation]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
$exe=Join-Path $root 'dist/VibeRa Studio/VibeRa Studio.exe'
if (-not $Project) { $Project=Join-Path $root 'dist/VibeRa Studio/Examples/Midnight Session.vibra' }
$Project=(Resolve-Path -LiteralPath $Project).Path
$openFlag=if($Detached) {'--detached-window-check-project'} else {'--window-check-project'}
$app=Start-Process -FilePath $exe -ArgumentList @($openFlag,('"'+$Project+'"')) -PassThru -WindowStyle Normal
$record=[System.Collections.Generic.List[string]]::new()
$ownedHandle=$app.Handle
$record.Add("Owned PID $($app.Id), creation $($app.StartTime.ToUniversalTime().ToString('o'))")
try {
 for($i=0;$i -lt 40;$i++) { Start-Sleep -Milliseconds 150; $app.Refresh(); if($app.MainWindowHandle -ne [IntPtr]::Zero) { break } }
 $h=if($Detached) {[VibeRaWindowObservation]::FindOwned([uint32]$app.Id,"VibeRa Studio")} else {$app.MainWindowHandle}
 $ownedPid=[uint32]0
 [VibeRaWindowObservation]::GetWindowThreadProcessId($h,[ref]$ownedPid) | Out-Null
 if($ownedPid -ne $app.Id) { throw 'Window ownership mismatch' }
 $record.Add("PID $($app.Id), created $($app.StartTime.ToUniversalTime().ToString('o')); executable SHA256 $((Get-FileHash -LiteralPath $exe).Hash)")
 function Capture([string]$name) {
  Start-Sleep -Milliseconds 450
  [VibeRaWindowObservation]::SetWindowPos($h,[IntPtr]::Zero,0,0,0,0,19) | Out-Null
  $r=New-Object VibeRaWindowObservation+Rect
  [VibeRaWindowObservation]::GetClientRect($h,[ref]$r) | Out-Null
  $bitmap=New-Object System.Drawing.Bitmap($r.right,$r.bottom)
  $g=[System.Drawing.Graphics]::FromImage($bitmap); $dc=$g.GetHdc()
  try { if(-not [VibeRaWindowObservation]::PrintWindow($h,$dc,3)) { throw 'PrintWindow failed' } }
  finally { $g.ReleaseHdc($dc); $g.Dispose() }
  try { $bitmap.Save((Join-Path $Output $name),[System.Drawing.Imaging.ImageFormat]::Png) }
  finally { $bitmap.Dispose() }
  $record.Add("$name $($r.right)x$($r.bottom), DPI $([VibeRaWindowObservation]::GetDpiForWindow($h)); native PrintWindow client capture, no foreground/keyboard/mouse input")
 }
 [VibeRaWindowObservation]::SetWindowPos($h,[IntPtr](-1),120,100,1920,1080,16) | Out-Null
 Capture 'native-dark-title-1080.png'
 $corner=0
 $cornerResult=[VibeRaWindowObservation]::DwmGetWindowAttribute($h,33,[ref]$corner,4)
 $record.Add("DWM corner preference result=$cornerResult value=$corner (requested round=2; OS-dependent)")
 if($cornerResult -eq 0 -and $corner -ne 2) { throw 'Native rounded corner preference not applied' }
 $r=New-Object VibeRaWindowObservation+Rect; $point=New-Object VibeRaWindowObservation+Point
 [VibeRaWindowObservation]::GetClientRect($h,[ref]$r) | Out-Null
 [VibeRaWindowObservation]::ClientToScreen($h,[ref]$point) | Out-Null
 $scale=[VibeRaWindowObservation]::GetDpiForWindow($h)/96.0
 foreach($probe in @(@('caption',300,18,2),@('minimize',($r.right/$scale-115),18,8),@('maximize',($r.right/$scale-69),18,9),@('close',($r.right/$scale-23),18,20),@('left edge',1,200,10))) {
  $x=$point.x+[int]($probe[1]*$scale); $y=$point.y+[int]($probe[2]*$scale)
  $packed=($y -shl 16) -bor ($x -band 65535)
  $hit=[VibeRaWindowObservation]::SendMessage($h,132,[IntPtr]::Zero,[IntPtr]$packed).ToInt64()
  $record.Add("Native WM_NCHITTEST $($probe[0])=$hit expected=$($probe[3])")
  if($hit -ne $probe[3]) { throw "Native hit test mismatch $($probe[0])" }
 }
 [VibeRaWindowObservation]::SendMessage($h,274,[IntPtr]61488,[IntPtr]::Zero) | Out-Null
 if(-not [VibeRaWindowObservation]::IsZoomed($h)) { throw 'Native maximize failed' }
 Capture 'native-dark-title-4k.png'
 [VibeRaWindowObservation]::SendMessage($h,274,[IntPtr]61728,[IntPtr]::Zero) | Out-Null
 if([VibeRaWindowObservation]::IsZoomed($h)) { throw 'Native restore failed' }
 [VibeRaWindowObservation]::SendMessage($h,274,[IntPtr]61472,[IntPtr]::Zero) | Out-Null
 if(-not [VibeRaWindowObservation]::IsIconic($h)) { throw 'Native minimize failed' }
 [VibeRaWindowObservation]::SendMessage($h,274,[IntPtr]61728,[IntPtr]::Zero) | Out-Null
 if($Detached) {
  $main=$h
  $mainRect=New-Object VibeRaWindowObservation+Rect
  [VibeRaWindowObservation]::GetWindowRect($main,[ref]$mainRect) | Out-Null
  Add-Type -AssemblyName System.Windows.Forms
  $screens=[System.Windows.Forms.Screen]::AllScreens
  $record.Add("OS-reported displays available: $($screens.Count)")
  foreach($screen in $screens) { $record.Add("Display $($screen.DeviceName): bounds=$($screen.Bounds) workarea=$($screen.WorkingArea)") }
  $mainScreen=[System.Windows.Forms.Screen]::FromHandle($main)
  $targetScreen=@($screens | Where-Object {$_.DeviceName -ne $mainScreen.DeviceName}) | Select-Object -First 1
  if(-not $targetScreen) { $targetScreen=$mainScreen }
  $targetArea=$targetScreen.WorkingArea
  foreach($name in @('Channel Rack','Piano Roll','Playlist','Mixer','Browser')) {
   $h=[VibeRaWindowObservation]::FindOwned([uint32]$app.Id,($name+' | VibeRa Studio'))
   if($h -eq [IntPtr]::Zero) { throw "Missing native editor: $name" }
   $id=[uint32]0; [VibeRaWindowObservation]::GetWindowThreadProcessId($h,[ref]$id) | Out-Null
   if($id -ne $app.Id) { throw 'Detached ownership mismatch' }
   [VibeRaWindowObservation]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
   $initialDpi=[VibeRaWindowObservation]::GetDpiForWindow($h)
   $targetX=$targetArea.Left+40; $targetY=$targetArea.Top+40
   if($screens.Count -eq 1) { $targetX=2200; $targetY=150 }
   [VibeRaWindowObservation]::SetWindowPos($h,[IntPtr]::Zero,$targetX,$targetY,[Math]::Min(1100,$targetArea.Width-80),[Math]::Min(700,$targetArea.Height-80),16) | Out-Null
   Start-Sleep -Milliseconds 150
   # Resize again after the peer's WM_DPICHANGED response has settled.
   # The first call moves the window; Windows/JUCE may adjust its size on DPI transition.
   [VibeRaWindowObservation]::SetWindowPos($h,[IntPtr]::Zero,$targetX,$targetY,[Math]::Min(1100,$targetArea.Width-80),[Math]::Min(700,$targetArea.Height-80),16) | Out-Null
   Start-Sleep -Milliseconds 200
   $actualScreen=[System.Windows.Forms.Screen]::FromHandle($h)
   if($actualScreen.DeviceName -ne $targetScreen.DeviceName) { throw "Editor did not reach requested display: $name" }
   $record.Add("$name display transition: $($mainScreen.DeviceName) -> $($actualScreen.DeviceName), DPI $initialDpi -> $([VibeRaWindowObservation]::GetDpiForWindow($h))")
   $detachedRect=New-Object VibeRaWindowObservation+Rect
   [VibeRaWindowObservation]::GetWindowRect($h,[ref]$detachedRect) | Out-Null
   if($detachedRect.left -lt $mainRect.right -and $detachedRect.right -gt $mainRect.left -and $detachedRect.top -lt $mainRect.bottom -and $detachedRect.bottom -gt $mainRect.top) { throw "Native editor overlaps main-window bounds: $name" }
   [VibeRaWindowObservation]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
   $client=New-Object VibeRaWindowObservation+Rect; $origin=New-Object VibeRaWindowObservation+Point
   [VibeRaWindowObservation]::GetClientRect($h,[ref]$client) | Out-Null
   [VibeRaWindowObservation]::ClientToScreen($h,[ref]$origin) | Out-Null
   $record.Add("$name native geometry: window=$($detachedRect.left),$($detachedRect.top),$($detachedRect.right),$($detachedRect.bottom); client=$($client.right)x$($client.bottom); origin=$($origin.x),$($origin.y)")
   $dpiScale=[VibeRaWindowObservation]::GetDpiForWindow($h)/96.0
   foreach($probe in @(@('caption',300,2),@('minimize',($client.right/$dpiScale-115),8),@('maximize',($client.right/$dpiScale-69),9),@('close',($client.right/$dpiScale-23),20))) {
    $px=$origin.x+[int]($probe[1]*$dpiScale); $py=$origin.y+[int](18*$dpiScale)
    $packed=($py -shl 16) -bor ($px -band 65535)
    $hit=[VibeRaWindowObservation]::SendMessage($h,132,[IntPtr]::Zero,[IntPtr]$packed).ToInt64()
    $record.Add("Detached $name $($probe[0]) hit=$hit expected=$($probe[2])")
    if($hit -ne $probe[2]) { throw 'Detached native title hit test failed after DPI change' }
   }
   Capture ('native-detached-'+($name.Replace(' ','-'))+'.png')
   [VibeRaWindowObservation]::SendMessage($h,274,[IntPtr]61488,[IntPtr]::Zero) | Out-Null
   if(-not [VibeRaWindowObservation]::IsZoomed($h)) { throw 'Detached maximize failed' }
   [VibeRaWindowObservation]::SendMessage($h,274,[IntPtr]61728,[IntPtr]::Zero) | Out-Null
   if([VibeRaWindowObservation]::IsZoomed($h)) { throw 'Detached restore failed' }
   [VibeRaWindowObservation]::SendMessage($h,274,[IntPtr]61472,[IntPtr]::Zero) | Out-Null
   if(-not [VibeRaWindowObservation]::IsIconic($h)) { throw 'Detached minimize failed' }
   [VibeRaWindowObservation]::SendMessage($h,274,[IntPtr]61728,[IntPtr]::Zero) | Out-Null
   $record.Add("PASS: $name owns an independent native window, moves beyond main bounds, resizes and minimizes/maximizes/restores through targeted OS window calls")
  }
  $h=$main
  $after=New-Object VibeRaWindowObservation+Rect
  [VibeRaWindowObservation]::GetWindowRect($main,[ref]$after) | Out-Null
  if($after.left -ne $mainRect.left -or $after.right -ne $mainRect.right -or $after.top -ne $mainRect.top -or $after.bottom -ne $mainRect.bottom) { throw 'Editor movement changed main bounds' }
 }
 $record.Add('PASS: native caption/button/edge hit tests and targeted system-command minimize/maximize/restore. No synthetic global input, physical input, Snap menu, audio playback or listening test. Detached display/DPI observations are recorded separately when requested.')
} catch { $record.Add("FAIL: $($_.Exception.Message)"); throw }
finally {
 if($h -ne [IntPtr]::Zero -and $ownedPid -eq $app.Id) { [VibeRaWindowObservation]::SetWindowPos($h,[IntPtr](-2),0,0,0,0,19) | Out-Null; [VibeRaWindowObservation]::PostMessage($h,16,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null }
 elseif(-not $app.HasExited) { $app.CloseMainWindow() | Out-Null }
 $gracefulShutdown=$app.WaitForExit(3000)
 if(-not $gracefulShutdown) { $app.Kill(); $app.WaitForExit(); $record.Add('Retained owned process handle terminated after graceful close timeout') }
 $record.Add("Owned native test terminal: $($app.HasExited); graceful=$gracefulShutdown; exitCode=$($app.ExitCode)")
 $record | Set-Content -LiteralPath (Join-Path $Output 'native-window-verification.txt') -Encoding UTF8
}
if(-not $gracefulShutdown -or $app.ExitCode -ne 0) { throw 'Native application did not close gracefully with exit code zero; see native-window-verification.txt' }
$record
