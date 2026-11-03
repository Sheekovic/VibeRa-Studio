param([string]$Project=(Join-Path $PSScriptRoot '../build/pattern-showcase.vibra'), [string]$Output=(Join-Path $PSScriptRoot '../build/pattern-os-input'), [switch]$UseGlobalInput, [string]$Executable='', [switch]$SplitByChannel, [switch]$TopmostOwnedWindow, [switch]$KeyboardSaveUndo, [ValidateSet('CtrlShiftZ','CtrlY')][string]$RedoShortcut='CtrlShiftZ', [switch]$Transpose, [switch]$RackWheel, [switch]$Picker, [switch]$StepFill, [switch]$ChannelSolo, [switch]$ChannelManagement, [switch]$StepVelocity, [switch]$Swing, [switch]$SwingDialogs, [switch]$SwingMenuProbe, [switch]$Quantizer, [switch]$Groove, [switch]$SamplerVoices, [switch]$LoopBounds)
$ErrorActionPreference='Stop'
if($Groove){$Quantizer=$true}
if(-not $UseGlobalInput){throw 'This script performs scoped OS input; pass -UseGlobalInput explicitly.'}
if($KeyboardSaveUndo -and -not $SplitByChannel){throw 'Keyboard save/undo requires the disposable SplitByChannel project'}
if($Transpose -and -not ($SplitByChannel -and $KeyboardSaveUndo)){throw 'Transpose needs the disposable keyboard workflow fixture'}
if($RackWheel -and -not $SplitByChannel){throw 'Rack wheel requires the disposable project fixture'}
if($Picker -and -not ($SplitByChannel -and $KeyboardSaveUndo)){throw 'Picker requires disposable keyboard fixture'}
$root=Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force -Path $Output | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class VibeRaPatternObservation {
 public delegate bool EnumCallback(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumCallback cb,IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,System.Text.StringBuilder text,int size);
 [DllImport("user32.dll")] public static extern void keybd_event(byte key,byte scan,uint flags,UIntPtr extra);
 public static string DescribeOwned(uint pid) {
  var rows=new System.Collections.Generic.List<string>();
  EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==pid){var t=new System.Text.StringBuilder(256);GetWindowText(h,t,256);rows.Add("Owned HWND "+h+" visible="+IsWindowVisible(h)+" title="+t);}return true;},IntPtr.Zero);
  return String.Join("\n",rows);
 }
 public static IntPtr FindOtherOwned(uint pid,IntPtr main,string title) {
  IntPtr found=IntPtr.Zero;
  EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);
   if(id==pid && h!=main && IsWindowVisible(h)) {
    var text=new System.Text.StringBuilder(256);GetWindowText(h,text,256);
    if(title=="" || text.ToString()==title){found=h;return false;}
   }return true;},IntPtr.Zero);return found;
 }

 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr root,EnumCallback cb,IntPtr p);
 [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
 [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,System.Text.StringBuilder text,int size);
 [DllImport("user32.dll",EntryPoint="SendMessageW",CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageText(IntPtr h,uint message,IntPtr w,string text);
 public static IntPtr FileNameEdit(IntPtr root,uint expectedPid) {
  var found=new System.Collections.Generic.List<IntPtr>();
  EnumChildWindows(root,(h,p)=>{
   if(!IsWindowVisible(h))return true;uint pid;GetWindowThreadProcessId(h,out pid);if(pid!=expectedPid)return true;
   var name=new System.Text.StringBuilder(128);GetClassName(h,name,128);
   if(name.ToString()!="Edit")return true;
   var parent=GetParent(h);var parentClass=new System.Text.StringBuilder(128);GetClassName(parent,parentClass,128);
   if(GetDlgCtrlID(h)==1001 && parentClass.ToString()=="ComboBox"){found.Add(h);return true;}
   for(int i=0;i<12 && parent!=IntPtr.Zero && parent!=root;i++,parent=GetParent(parent)){
    if(GetDlgCtrlID(parent)==1148){found.Add(h);break;}
   }return true;
  },IntPtr.Zero);return found.Count==1?found[0]:IntPtr.Zero;
 }
 public static string DescribeChildClasses(IntPtr root) {
  var rows=new System.Collections.Generic.List<string>();EnumChildWindows(root,(h,p)=>{if(IsWindowVisible(h)){var name=new System.Text.StringBuilder(128);GetClassName(h,name,128);rows.Add(name+" id="+GetDlgCtrlID(h));}return true;},IntPtr.Zero);return String.Join("; ",rows);
 }
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
 [DllImport("user32.dll")] public static extern IntPtr GetKeyboardLayout(uint thread);
 [DllImport("user32.dll")] public static extern uint MapVirtualKeyEx(uint code,uint map,IntPtr layout);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
}
'@
[VibeRaPatternObservation]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
$exe=if($Executable){(Resolve-Path -LiteralPath $Executable).Path}else{Join-Path $root 'dist/VibeRa Studio/VibeRa Studio.exe'}
if($exe -notin @((Join-Path $root 'dist/VibeRa Studio/VibeRa Studio.exe'),(Join-Path $root 'build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe'))){throw 'Use only the canonical application or its current compilation output'}
$Project=(Resolve-Path -LiteralPath $Project).Path
if($SplitByChannel -or $StepFill -or $ChannelSolo -or $ChannelManagement -or $StepVelocity -or $Swing -or $Quantizer -or $SamplerVoices -or $LoopBounds){
 $Output=[IO.Path]::GetFullPath($Output)
 if(-not $Output.StartsWith((Join-Path $root 'build')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Split test writes only a disposable project under build'}
 $ownedProject=Join-Path $Output $(if($LoopBounds){'native-loop-bounds-input.vibra'}elseif($SamplerVoices){'native-sampler-voices-input.vibra'}elseif($Quantizer){'native-quantizer-input.vibra'}elseif($Swing){'native-swing-input.vibra'}elseif($StepVelocity){'native-step-velocity-input.vibra'}elseif($StepFill){'native-step-fill-input.vibra'}elseif($ChannelSolo){'native-channel-solo-input.vibra'}elseif($ChannelManagement){'native-channel-management-input.vibra'}else{'native-split-input.vibra'})
 if(Test-Path -LiteralPath $ownedProject){throw 'Use a fresh output folder to preserve previous native evidence'}
 $inputXml=[xml](Get-Content -LiteralPath $Project -Raw)
 if(@($inputXml.VIBERA.CHANNELS.CHANNEL | Where-Object {$_.source -notlike 'builtin:*' -or $_.PLUGIN}).Count){throw 'Native split fixture must use original built-in instruments only'}
 Copy-Item -LiteralPath $Project -Destination $ownedProject
 if($Swing){$swingXml=[xml](Get-Content -LiteralPath $ownedProject -Raw);$swingXml.VIBERA.SetAttribute("swing","0");$swingXml.Save($ownedProject)}
 $Project=$ownedProject
}
if($ChannelManagement -or $StepVelocity -or $Swing -or $Quantizer -or $SamplerVoices -or $LoopBounds){
 $start=New-Object System.Diagnostics.ProcessStartInfo
 $start.FileName=$exe
 $start.Arguments='--window-check-project "'+$Project+'" --mcp-test'
 $start.UseShellExecute=$false
 $start.CreateNoWindow=$true
 $start.RedirectStandardInput=$true
 $start.RedirectStandardOutput=$true
 $start.StandardOutputEncoding=[Text.Encoding]::UTF8
 $app=New-Object System.Diagnostics.Process
 $app.StartInfo=$start
 if(-not $app.Start()){throw 'Owned native/MCP app did not start'}
}else{
 $app=Start-Process -FilePath $exe -ArgumentList @('--window-check-project',('"'+$Project+'"')) -WindowStyle Normal -PassThru
}
$ownedHandle=$app.Handle
$creation=$app.StartTime.ToUniversalTime().ToString('o')
$record=[System.Collections.Generic.List[string]]::new()
$record.Add("Owned PID $($app.Id), creation $creation, executable SHA256 $((Get-FileHash -LiteralPath $exe).Hash)")
$cursor=New-Object VibeRaPatternObservation+Point
[VibeRaPatternObservation]::GetCursorPos([ref]$cursor) | Out-Null
$script:ownedPointerDown=$false

try {
 for($i=0;$i -lt 40;$i++) { Start-Sleep -Milliseconds 150; $app.Refresh(); if($app.MainWindowHandle -ne [IntPtr]::Zero) { break } }
 $h=$app.MainWindowHandle
 if($h -eq [IntPtr]::Zero) {throw 'Owned main window missing'}
 function Assert-ActiveOwned {
  $id=[uint32]0; [VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::GetForegroundWindow(),[ref]$id) | Out-Null
  # Windows may briefly report no foreground HWND during owned activation.
  # Wait without injecting anything; a different process still pauses immediately.
  for($retry=0;$id -eq 0 -and -not $app.HasExited -and $retry -lt 10;$retry++){
   Start-Sleep -Milliseconds 50
   [VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::GetForegroundWindow(),[ref]$id)|Out-Null
  }
  if($id -ne $app.Id -or $app.HasExited) {$record.Add("Foreground PID $id; expected owned PID $($app.Id); exited=$($app.HasExited)");throw 'Foreground ownership lost; input paused'}
  foreach($key in (@(1,2,13,16,17,18,27,91,92)+@(32..90))) {
   if(([VibeRaPatternObservation]::GetAsyncKeyState($key) -band 32768) -ne 0) {throw 'User input is active; input paused'}
  }
 }
 function Click([double]$x,[double]$y) {
  Assert-ActiveOwned
  $origin=New-Object VibeRaPatternObservation+Point; [VibeRaPatternObservation]::ClientToScreen($h,[ref]$origin) | Out-Null
  $point=New-Object VibeRaPatternObservation+Point; $point.x=$origin.x+[int]($x*$scale);$point.y=$origin.y+[int](($y+36)*$scale)
  $id=[uint32]0;[VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::WindowFromPoint($point),[ref]$id) | Out-Null
  if($id -ne $app.Id) {throw 'Owned click target obscured; input paused'}
  [VibeRaPatternObservation]::SetCursorPos($point.x,$point.y) | Out-Null
  [VibeRaPatternObservation]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [VibeRaPatternObservation]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
  Start-Sleep -Milliseconds 180
 }
 function Right-Click([double]$x,[double]$y) {
  Assert-ActiveOwned
  $origin=New-Object VibeRaPatternObservation+Point; [VibeRaPatternObservation]::ClientToScreen($h,[ref]$origin) | Out-Null
  $point=New-Object VibeRaPatternObservation+Point; $point.x=$origin.x+[int]($x*$scale);$point.y=$origin.y+[int](($y+36)*$scale)
  $id=[uint32]0;[VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::WindowFromPoint($point),[ref]$id) | Out-Null
  if($id -ne $app.Id) {throw 'Owned click target obscured; input paused'}
  [VibeRaPatternObservation]::SetCursorPos($point.x,$point.y) | Out-Null
  [VibeRaPatternObservation]::mouse_event(8,0,0,0,[UIntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [VibeRaPatternObservation]::mouse_event(16,0,0,0,[UIntPtr]::Zero)
  Start-Sleep -Milliseconds 180
 }
 function Key([byte]$key) {
  Assert-ActiveOwned
  $scan=Scan-Code $key
  $extended=if($key -in @(33,34,35,36,37,38,39,40,45,46)){[uint32]1}else{[uint32]0}
  [VibeRaPatternObservation]::keybd_event($key,$scan,$extended,[UIntPtr]::Zero)
  [VibeRaPatternObservation]::keybd_event($key,$scan,($extended -bor 2),[UIntPtr]::Zero)
  Start-Sleep -Milliseconds 180
 }
 function Scan-Code([byte]$key) {
  $owner=[uint32]0
  $thread=[VibeRaPatternObservation]::GetWindowThreadProcessId($h,[ref]$owner)
  if($owner -ne $app.Id){throw 'Keyboard target ownership changed'}
  $layout=[VibeRaPatternObservation]::GetKeyboardLayout($thread)
  $scan=[VibeRaPatternObservation]::MapVirtualKeyEx($key,0,$layout)
  if($scan -eq 0 -or $scan -gt 255){throw 'Unsupported key scan code; no keyboard input'}
  $record.Add("Native key VK=$key scan=$scan targetKeyboardLayout=$layout")
  return [byte]$scan
 }
 function Control-Key([byte]$key,[switch]$Shift) {
  Assert-ActiveOwned
  $scan=Scan-Code $key
  $controlScan=Scan-Code 17
  $shiftScan=if($Shift){Scan-Code 16}else{0}
  [VibeRaPatternObservation]::keybd_event(17,$controlScan,0,[UIntPtr]::Zero)
  try {
   if($Shift){[VibeRaPatternObservation]::keybd_event(16,$shiftScan,0,[UIntPtr]::Zero)}
   Start-Sleep -Milliseconds 60
   $heldControl=([VibeRaPatternObservation]::GetAsyncKeyState(17) -band 32768) -ne 0
   $heldShift=([VibeRaPatternObservation]::GetAsyncKeyState(16) -band 32768) -ne 0
   $record.Add("Injected chord before key: Control=$heldControl Shift=$heldShift requestedShift=$Shift")
   if(-not $heldControl -or $heldShift -ne [bool]$Shift){throw 'Injected modifier state does not match intended chord'}
   [VibeRaPatternObservation]::keybd_event($key,$scan,0,[UIntPtr]::Zero)
   Start-Sleep -Milliseconds 60
   [VibeRaPatternObservation]::keybd_event($key,$scan,2,[UIntPtr]::Zero)
   Start-Sleep -Milliseconds 40
  } finally {
   if($Shift){[VibeRaPatternObservation]::keybd_event(16,$shiftScan,2,[UIntPtr]::Zero)}
   [VibeRaPatternObservation]::keybd_event(17,$controlScan,2,[UIntPtr]::Zero)
  }
  Start-Sleep -Milliseconds 250
 }
 function Alt-Key([byte]$key,[byte]$modifier=18) {
  Assert-ActiveOwned
  $scan=Scan-Code $key
  $modifierScan=Scan-Code $modifier
  $flags=if($key -in @(33,34,35,36,37,38,39,40,45,46)){[uint32]1}else{[uint32]0}
  [VibeRaPatternObservation]::keybd_event($modifier,$modifierScan,0,[UIntPtr]::Zero)
  try {
   Start-Sleep -Milliseconds 60
   if(([VibeRaPatternObservation]::GetAsyncKeyState($modifier) -band 32768) -eq 0){throw 'Injected modifier state missing'}
   [VibeRaPatternObservation]::keybd_event($key,$scan,$flags,[UIntPtr]::Zero)
   Start-Sleep -Milliseconds 60
   [VibeRaPatternObservation]::keybd_event($key,$scan,($flags -bor 2),[UIntPtr]::Zero)
  } finally { [VibeRaPatternObservation]::keybd_event($modifier,$modifierScan,2,[UIntPtr]::Zero) }
  Start-Sleep -Milliseconds 250
 }
 function Capture([IntPtr]$window,[string]$name) {
  $id=[uint32]0;[VibeRaPatternObservation]::GetWindowThreadProcessId($window,[ref]$id) | Out-Null
  if($id -ne $app.Id) {throw 'Capture ownership mismatch'}
  $r=New-Object VibeRaPatternObservation+Rect;[VibeRaPatternObservation]::GetClientRect($window,[ref]$r) | Out-Null
  $bitmap=New-Object System.Drawing.Bitmap($r.right,$r.bottom);$graphics=[System.Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
  try {if(-not [VibeRaPatternObservation]::PrintWindow($window,$dc,3)){throw 'Owned native capture failed'}}
  finally {$graphics.ReleaseHdc($dc);$graphics.Dispose()}
  try {$bitmap.Save((Join-Path $Output $name),[System.Drawing.Imaging.ImageFormat]::Png)}finally {$bitmap.Dispose()}
  $record.Add("$name $($r.right)x$($r.bottom), DPI $([VibeRaPatternObservation]::GetDpiForWindow($window)); native owned-window PrintWindow")
 }
 [VibeRaPatternObservation]::SetWindowPos($h,[IntPtr]::Zero,120,100,1920,1080,16) | Out-Null
 if($TopmostOwnedWindow){
  foreach($key in (@(1,2,13,16,17,18,27,91,92)+@(32..90))){
   if(([VibeRaPatternObservation]::GetAsyncKeyState($key) -band 32768) -ne 0){throw 'User input is active; no window raising or input'}
  }
  $owner=[uint32]0;[VibeRaPatternObservation]::GetWindowThreadProcessId($h,[ref]$owner) | Out-Null
  if($owner -ne $app.Id){throw 'Refuse raising a window outside the owned test process'}
  [VibeRaPatternObservation]::SetWindowPos($h,[IntPtr](-1),0,0,0,0,19) | Out-Null
  $record.Add('Raised only the owned temporary test HWND above the occluding editor; no unrelated window was modified. Test instance closes at exit.')
 }
 Start-Sleep -Milliseconds 300
 [VibeRaPatternObservation]::SetForegroundWindow($h) | Out-Null
 if([VibeRaPatternObservation]::GetForegroundWindow() -ne $h) {
  if(([VibeRaPatternObservation]::GetAsyncKeyState(1) -band 32768) -ne 0){throw 'Mouse active; no activation input'}
  $origin=New-Object VibeRaPatternObservation+Point;[VibeRaPatternObservation]::ClientToScreen($h,[ref]$origin) | Out-Null
  $point=New-Object VibeRaPatternObservation+Point;$point.x=$origin.x+450;$point.y=$origin.y+27
  $hit=[VibeRaPatternObservation]::WindowFromPoint($point)
  $targetPid=[uint32]0
  [VibeRaPatternObservation]::GetWindowThreadProcessId($hit,[ref]$targetPid) | Out-Null
  if($targetPid -ne $app.Id){throw "Owned caption obscured by PID $targetPid; expected $($app.Id); no activation input"}
  [VibeRaPatternObservation]::SetCursorPos($point.x,$point.y) | Out-Null
  [VibeRaPatternObservation]::mouse_event(2,0,0,0,[UIntPtr]::Zero);[VibeRaPatternObservation]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
  Start-Sleep -Milliseconds 180
 }
 if($ChannelManagement -or $StepVelocity -or $Swing -or $Quantizer -or $SamplerVoices -or $LoopBounds){
  $script:nativeRpcId=0
  function Native-Rpc([string]$method,$parameters) {
   $script:nativeRpcId++
   $request=@{jsonrpc='2.0';id=$script:nativeRpcId;method=$method;params=$parameters}|ConvertTo-Json -Depth 16 -Compress
   $app.StandardInput.WriteLine($request);$app.StandardInput.Flush()
   $reply=$app.StandardOutput.ReadLineAsync()
   if(-not $reply.Wait(5000)){throw 'Read-only native MCP diagnostic timed out'}
   $value=$reply.Result|ConvertFrom-Json
   if($value.id -ne $script:nativeRpcId -or $value.error){throw 'Native MCP diagnostic reply invalid'}
   return $value.result
  }
  Native-Rpc 'initialize' @{protocolVersion='2025-11-25';capabilities=@{};clientInfo=@{name='VibeRa native readback';version='1'}}|Out-Null
  $app.StandardInput.WriteLine('{"jsonrpc":"2.0","method":"notifications/initialized"}');$app.StandardInput.Flush()
  function Native-State([string]$name) {
   $state=(Native-Rpc 'tools/call' @{name='vibera_state';arguments=@{}}).structuredContent
   $state|ConvertTo-Json -Depth 32|Set-Content -LiteralPath (Join-Path $Output ('native-state-'+$name+'.json')) -Encoding UTF8
   $record.Add("Native read-only MCP state $name scope=$($state.scope) Channels=$(@($state.channels).Count) active=$($state.selectedChannel)")
   return $state
  }
 }
 Assert-ActiveOwned
 $scale=[VibeRaPatternObservation]::GetDpiForWindow($h)/96.0
 if($LoopBounds){
  $before=Native-State 'loop-before'
  $pattern=[long]$before.selectedPattern
  $originalLength=@($before.patterns|Where-Object {$_.id -eq $pattern})[0].length
  $revision=$before.revision
  $wasEnabled=[bool]$before.audio.loopBounds.enabled
  Click 255 56
  $disabled=Native-State 'loop-toggle'
  if([bool]$disabled.audio.loopBounds.enabled -eq $wasEnabled -or $disabled.revision -ne $revision){throw 'Native Loop toggle did not change runtime state independently of document history'}
  Click 255 56
  if([bool](Native-State 'loop-restored').audio.loopBounds.enabled -ne $wasEnabled){throw 'Native Loop second toggle failed'}
  $rack=@($before.editors|Where-Object {$_.index -eq 0})[0].contentBounds
  Click ($rack[0]+160) ($rack[1]+31)
  Key 36;Key 40;Key 13
  $grown=Native-State 'loop-length'
  if(@($grown.patterns|Where-Object {$_.id -eq $pattern})[0].length -ne 8 -or $grown.audio.loopBounds.endBeat -ne 8){throw 'Native Rack 2-bar length menu failed'}
  Control-Key 83
  $saved=[xml](Get-Content -LiteralPath $Project -Raw)
  if([double]@($saved.VIBERA.PATTERNS.PATTERN|Where-Object {[long]$_.id -eq $pattern})[0].length -ne 8){throw 'Native Pattern length Save failed'}
  Control-Key 90
  $undo=Native-State 'loop-undo'
  if(@($undo.patterns|Where-Object {$_.id -eq $pattern})[0].length -ne $originalLength -or $undo.audio.loopBounds.endBeat -ne $originalLength){throw 'Native Pattern length Undo failed'}
  Control-Key 89
  if((Native-State 'loop-redo').audio.loopBounds.endBeat -ne 8){throw 'Native Pattern length Redo failed'}
  # Pattern/Song defaults are preserved. Song loop is an explicit new opt-in.
  Click 197 56
  $song=Native-State 'loop-song'
  if($song.audio.loopBounds.enabled){throw 'Song default unexpectedly loops'}
  Click 255 56
  $looping=Native-State 'loop-song-enabled'
  if(-not $looping.audio.loopBounds.enabled -or $looping.audio.loopBounds.endBeat -le 8){throw 'Native whole-Song looping failed'}
  Capture $h 'native-loop-workspace.png'
  Control-Key 83
  $record.Add('PASS native Loop icon toggle, runtime/document separation, Rack 2-bar menu, exact Undo/Redo/Save and whole-Song opt-in. No physical audio/listening claim; no-device applied bounds remain unavailable.')
  return
 }
 if($SamplerVoices){
  $initial=Native-State 'voices-initial'
  $rack=@($initial.editors|Where-Object {$_.index -eq 0})[0].contentBounds
  $target=[long]$initial.channels[0].id
  Click ($rack[0]+90) ($rack[1]+60)
  $title=$initial.channels[0].name+' | Channel instrument'
  $dialog=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,$title)
  if($dialog -eq [IntPtr]::Zero){throw 'Native Channel name did not open instrument'}
  [VibeRaPatternObservation]::SetWindowPos($dialog,[IntPtr](-1),0,0,0,0,19)|Out-Null
  [VibeRaPatternObservation]::SetForegroundWindow($dialog)|Out-Null
  Start-Sleep -Milliseconds 150
  function Voice-Click([string]$id,[bool]$textEntry=$false){
   Assert-ActiveOwned
   $view=Native-State ('voices-control-'+$id)
   $window=@($view.samplerWindows|Where-Object {$_.channel -eq $target})[0]
   $r=$window.controls.PSObject.Properties['instrument.sampler.'+$id].Value
   if(-not $r -or $r.width -lt 20){throw 'Sampler control missing'}
   $dpi=[VibeRaPatternObservation]::GetDpiForWindow($dialog)/96.0
   $origin=New-Object VibeRaPatternObservation+Point
   [VibeRaPatternObservation]::ClientToScreen($dialog,[ref]$origin)|Out-Null
   $point=New-Object VibeRaPatternObservation+Point
   $x=if($textEntry){$r.x+25}else{$r.x+$r.width/2}
   $point.x=$origin.x+[int]($x*$dpi);$point.y=$origin.y+[int](($r.y+$r.height/2)*$dpi)
   $pidAt=[uint32]0;[VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::WindowFromPoint($point),[ref]$pidAt)|Out-Null
   if($pidAt -ne $app.Id){throw 'Sampler control obscured; input paused'}
   [VibeRaPatternObservation]::SetCursorPos($point.x,$point.y)|Out-Null
   Start-Sleep -Milliseconds 150
   $actual=New-Object VibeRaPatternObservation+Point;[VibeRaPatternObservation]::GetCursorPos([ref]$actual)|Out-Null
   if($actual.x -ne $point.x -or $actual.y -ne $point.y){throw 'User pointer activity; input paused'}
   Assert-ActiveOwned
   [VibeRaPatternObservation]::mouse_event(2,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 40
   [VibeRaPatternObservation]::mouse_event(4,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 180
  }
  $normalizeBefore=Native-State 'normalize-before'
  Voice-Click 'normalize'
  $normalized=Native-State 'normalize-enabled'
  if(-not @($normalized.channels|Where-Object {$_.id -eq $target})[0].sampler.normalize){throw 'Native Normalize checkbox failed'}
  Capture $dialog 'native-sampler-normalize.png'
  [VibeRaPatternObservation]::SendMessage($dialog,16,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  [VibeRaPatternObservation]::SetForegroundWindow($h)|Out-Null;Start-Sleep -Milliseconds 150
  Control-Key 83
  $normalizedFile=[xml](Get-Content -LiteralPath $Project -Raw)
  if([int]@($normalizedFile.VIBERA.CHANNELS.CHANNEL|Where-Object {[long]$_.id -eq $target})[0].SAMPLER.normalize -ne 1){throw 'Native Save lost Normalize'}
  Control-Key 90
  if(@((Native-State 'normalize-undo').channels|Where-Object {$_.id -eq $target})[0].sampler.normalize){throw 'Native Normalize Undo failed'}
  Control-Key 89
  if(-not @((Native-State 'normalize-redo').channels|Where-Object {$_.id -eq $target})[0].sampler.normalize){throw 'Native Normalize Redo failed'}
  Control-Key 90;Control-Key 83
  Click ($rack[0]+90) ($rack[1]+60)
  $dialog=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,$title)
  if($dialog -eq [IntPtr]::Zero){throw 'Native Normalize instrument reopen failed'}
  [VibeRaPatternObservation]::SetWindowPos($dialog,[IntPtr](-1),0,0,0,0,19)|Out-Null
  [VibeRaPatternObservation]::SetForegroundWindow($dialog)|Out-Null;Start-Sleep -Milliseconds 150
  $record.Add('PASS native Normalize checkbox, requested/prepared readback, actual project Save, one Undo/Redo and original setting restored. Not listening or hardware.')
  $before=Native-State 'voices-before'
  foreach($field in @('cut','cutBy','maxPoly')){
   Voice-Click $field $true;Control-Key 65;Key 52;Key 13
   $changed=Native-State ('voices-'+$field)
   $settings=@($changed.channels|Where-Object {$_.id -eq $target})[0].sampler
   if($settings.PSObject.Properties[$field].Value -ne 4){throw "Native $field text entry failed"}
  }
  # Drag the actual JUCE increment button, not an MCP substitute.
  $dragView=Native-State 'voices-drag-start'
  $dragRect=@($dragView.samplerWindows|Where-Object {$_.channel -eq $target})[0].controls.'instrument.sampler.maxPoly'
  $origin=New-Object VibeRaPatternObservation+Point
  [VibeRaPatternObservation]::ClientToScreen($dialog,[ref]$origin)|Out-Null
  $dpi=[VibeRaPatternObservation]::GetDpiForWindow($dialog)/96.0
  $point=New-Object VibeRaPatternObservation+Point
  $point.x=$origin.x+[int](($dragRect.x+$dragRect.width-25)*$dpi)
  $point.y=$origin.y+[int](($dragRect.y+$dragRect.height/2)*$dpi)
  Assert-ActiveOwned
  $hitPid=[uint32]0;[VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::WindowFromPoint($point),[ref]$hitPid)|Out-Null
  if($hitPid -ne $app.Id){throw 'Voice drag start obscured'}
  [VibeRaPatternObservation]::SetCursorPos($point.x,$point.y)|Out-Null
  [VibeRaPatternObservation]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
  try {
   for($step=1;$step -le 6;$step++){
    Start-Sleep -Milliseconds 70
    $activePid=[uint32]0;[VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::GetForegroundWindow(),[ref]$activePid)|Out-Null
    if($activePid -ne $app.Id){throw 'Voice drag lost foreground; input paused'}
    $actual=New-Object VibeRaPatternObservation+Point;[VibeRaPatternObservation]::GetCursorPos([ref]$actual)|Out-Null
    if($actual.x -ne $point.x -or $actual.y -ne $point.y){throw 'User pointer activity; voice drag paused'}
    foreach($key in (@(2,13,16,17,18,27,91,92)+@(32..90))){if(([VibeRaPatternObservation]::GetAsyncKeyState($key) -band 32768) -ne 0){throw 'User input active; voice drag paused'}}
    $point.x+=12
    [VibeRaPatternObservation]::SetCursorPos($point.x,$point.y)|Out-Null
   }
  }finally{[VibeRaPatternObservation]::mouse_event(4,0,0,0,[UIntPtr]::Zero)}
  Start-Sleep -Milliseconds 180
  $dragged=Native-State 'voices-drag-end'
  $dragValue=@($dragged.channels|Where-Object {$_.id -eq $target})[0].sampler.maxPoly
  if($dragValue -le 4 -or $dragValue -gt 99){throw 'Native voice drag did not change the bounded parameter'}
  Control-Key 90
  if(@((Native-State 'voices-drag-undo').channels|Where-Object {$_.id -eq $target})[0].sampler.maxPoly -ne 4){throw 'Native voice drag was not one Undo'}
  Control-Key 89
  if(@((Native-State 'voices-drag-redo').channels|Where-Object {$_.id -eq $target})[0].sampler.maxPoly -ne $dragValue){throw 'Native voice drag Redo mismatch'}
  Control-Key 90
  Capture $dialog 'native-sampler-voices.png'
  Voice-Click 'cutItself'
  $changed=Native-State 'voices-cut-itself'
  $settings=@($changed.channels|Where-Object {$_.id -eq $target})[0].sampler
  if($settings.cut -ne $settings.cutBy -or $settings.cut -le 0){throw 'Native Cut itself failed'}
  # Close only the owned instrument. Main editor shortcuts must still work.
  $owner=[uint32]0;[VibeRaPatternObservation]::GetWindowThreadProcessId($dialog,[ref]$owner)|Out-Null
  if($owner -ne $app.Id){throw 'Instrument ownership changed'}
  [VibeRaPatternObservation]::SendMessage($dialog,16,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  [VibeRaPatternObservation]::SetForegroundWindow($h)|Out-Null;Start-Sleep -Milliseconds 150
  Control-Key 83
  $saved=[xml](Get-Content -LiteralPath $Project -Raw)
  $savedSettings=@($saved.VIBERA.CHANNELS.CHANNEL|Where-Object {[long]$_.id -eq $target})[0].SAMPLER
  if([int]$savedSettings.maxPoly -ne 4 -or [int]$savedSettings.cut -ne [int]$settings.cut){throw 'Native save lost voice settings'}
  # Four controls create four changes (the idempotent Cut itself may create none).
  $selfChanged=$settings.cut -ne 4 -or $settings.cutBy -ne 4
  $count=3+[int]$selfChanged
  for($i=0;$i -lt $count;$i++){Control-Key 90}
  $undone=Native-State 'voices-undo'
  foreach($field in @('cut','cutBy','maxPoly')){
   if(@($undone.channels|Where-Object {$_.id -eq $target})[0].sampler.PSObject.Properties[$field].Value -ne @($before.channels|Where-Object {$_.id -eq $target})[0].sampler.PSObject.Properties[$field].Value){throw 'Native voice Undo mismatch'}
  }
  for($i=0;$i -lt $count;$i++){if($RedoShortcut -eq 'CtrlY'){Control-Key 89}else{Control-Key 90 -Shift}}
  $redone=Native-State 'voices-redo'
  if(@($redone.channels|Where-Object {$_.id -eq $target})[0].sampler.maxPoly -ne 4){throw 'Native voice Redo mismatch'}
  Control-Key 83
  Capture $h 'native-sampler-workspace.png'
  for($i=0;$i -lt $count;$i++){Control-Key 90}
  Right-Click ($rack[0]+90) ($rack[1]+60)
  Capture $h 'native-sampler-context-menu.png'
  Key 36;Key 40;Key 40;Key 40;Key 13
  $context=Native-State 'voices-context'
  $contextSettings=@($context.channels|Where-Object {$_.id -eq $target})[0].sampler
  if($contextSettings.cut -le 0 -or $contextSettings.cut -ne $contextSettings.cutBy){throw 'Native Rack Cut-itself context action failed'}
  Control-Key 90
  $contextUndo=Native-State 'voices-context-undo'
  if(@($contextUndo.channels|Where-Object {$_.id -eq $target})[0].sampler.cut -ne @($before.channels|Where-Object {$_.id -eq $target})[0].sampler.cut){throw 'Native context Cut-itself Undo failed'}
  Control-Key 83
  $record.Add('PASS native Cut/Cut-by/Max-poly text entries, actual continuous increment-button drag/one Undo/Redo, Cut-itself, shared readback, exact control Undo/Redo and project Save. No OS audio-device/listening claim; Rack context Cut-itself and its exact Undo also passed.')
  return
 }
 if($Quantizer){
  function Score($state){return (@($state.notes|ForEach-Object {@{id=$_.id;channel=$_.channel;beat=$_.beat;length=$_.length;pitch=$_.pitch;velocity=$_.velocity;step=$_.step}})|ConvertTo-Json -Depth 5 -Compress)}
  $bootstrap=Native-State 'quantizer-bootstrap'
  $rack=@($bootstrap.editors|Where-Object {$_.index -eq 0})[0].contentBounds
  Click ($rack[0]+11) ($rack[1]+60)
  $piano=@($bootstrap.editors|Where-Object {$_.index -eq 1})[0].contentBounds
  Click ($piano[0]+260) ($piano[1]+14)
  $before=Native-State 'quantizer-before'
  if($before.scope -ne 2 -or $before.selectedChannel -ne $before.channels[0].id){throw 'Native editor focus/Channel selection incorrect'}
  $original=Score $before
  Alt-Key 81 16
  $quickStarts=Native-State 'quantizer-quick-starts'
  $startNote=@($quickStarts.notes|Where-Object {$_.channel -eq $before.selectedChannel})[0]
  $originalNote=@($before.notes|Where-Object {$_.id -eq $startNote.id})[0]
  if($startNote.beat -ne 0 -or $startNote.length -ne $originalNote.length){throw 'Native Shift+Q start-only behavior incorrect'}
  Control-Key 90
  if((Score (Native-State 'quantizer-quick-starts-undo')) -ne $original){throw 'Native quick-start Undo incorrect'}
  Control-Key 81
  $quick=Native-State 'quantizer-quick-both'
  $quickNote=@($quick.notes|Where-Object {$_.id -eq $startNote.id})[0]
  if($quickNote.beat -ne 0 -or $quickNote.length -ne .25){throw 'Native Ctrl+Q start/duration behavior incorrect'}
  Control-Key 90
  if((Score (Native-State 'quantizer-quick-both-undo')) -ne $original){throw 'Native quick timing Undo incorrect'}
  function Open-Quantizer {
   Alt-Key 81
   $dialog=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,'Quantizer')
   if($dialog -eq [IntPtr]::Zero){throw 'Native Alt+Q did not open Quantizer'}
   $owner=[uint32]0;[VibeRaPatternObservation]::GetWindowThreadProcessId($dialog,[ref]$owner)|Out-Null
   if($owner -ne $app.Id){throw 'Quantizer ownership mismatch'}
   Assert-ActiveOwned
   [VibeRaPatternObservation]::SetWindowPos($dialog,[IntPtr](-1),0,0,0,0,19)|Out-Null
   [VibeRaPatternObservation]::SetForegroundWindow($dialog)|Out-Null
   Start-Sleep -Milliseconds 150
   Assert-ActiveOwned
   return $dialog
  }
  $dialog=Open-Quantizer
  if($Groove){
   function Dialog-Click([string]$id,[bool]$textEntry=$false){
    Assert-ActiveOwned
    $model=Native-State ('groove-control-'+$id)
    $rectangle=$model.quantizerWindow.controls.PSObject.Properties[$id].Value
    if(-not $rectangle -or -not $rectangle.enabled){throw "Missing/disabled owned dialog control $id"}
    $dpi=[VibeRaPatternObservation]::GetDpiForWindow($dialog)/96.0
    $origin=New-Object VibeRaPatternObservation+Point;[VibeRaPatternObservation]::ClientToScreen($dialog,[ref]$origin)|Out-Null
    $point=New-Object VibeRaPatternObservation+Point
    $logicalX=if($textEntry){$rectangle.x+$rectangle.width-36}else{$rectangle.x+$rectangle.width/2}
    $point.x=$origin.x+[int]($logicalX*$dpi);$point.y=$origin.y+[int](($rectangle.y+$rectangle.height/2)*$dpi)
    $hit=[VibeRaPatternObservation]::WindowFromPoint($point);$owner=[uint32]0;[VibeRaPatternObservation]::GetWindowThreadProcessId($hit,[ref]$owner)|Out-Null
    if($owner -ne $app.Id){throw 'Owned groove dialog control is obscured; input paused'}
    [VibeRaPatternObservation]::SetCursorPos($point.x,$point.y)|Out-Null
    Start-Sleep -Milliseconds 150
    $actual=New-Object VibeRaPatternObservation+Point;[VibeRaPatternObservation]::GetCursorPos([ref]$actual)|Out-Null
    if($actual.x -ne $point.x -or $actual.y -ne $point.y){throw 'User pointer activity; input paused'}
    Assert-ActiveOwned
    [VibeRaPatternObservation]::mouse_event(2,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 40
    [VibeRaPatternObservation]::mouse_event(4,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 200
   }
   function File-Choice([bool]$writing,[string]$path){
    if($writing){Dialog-Click 'quantizer.save';$title='Save VibeRa groove'}else{Dialog-Click 'quantizer.load';$title='Load VibeRa groove'}
    $fileDialog=[IntPtr]::Zero
    for($i=0;$i -lt 30 -and $fileDialog -eq [IntPtr]::Zero;$i++){
     Start-Sleep -Milliseconds 100;$fileDialog=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,$title)
    }
    if($fileDialog -eq [IntPtr]::Zero){$record.Add([VibeRaPatternObservation]::DescribeOwned([uint32]$app.Id));throw 'Owned native groove file dialog unavailable'}
    Assert-ActiveOwned
    [VibeRaPatternObservation]::SetWindowPos($fileDialog,[IntPtr](-1),0,0,0,0,19)|Out-Null
    [VibeRaPatternObservation]::SetForegroundWindow($fileDialog)|Out-Null
    Start-Sleep -Milliseconds 100;Assert-ActiveOwned
    $edit=[VibeRaPatternObservation]::FileNameEdit($fileDialog,[uint32]$app.Id)
    if($edit -eq [IntPtr]::Zero){$record.Add([VibeRaPatternObservation]::DescribeChildClasses($fileDialog));throw 'No unambiguous owned filename edit; no text injected'}
    $owner=[uint32]0;[VibeRaPatternObservation]::GetWindowThreadProcessId($edit,[ref]$owner)|Out-Null
    if($owner -ne $app.Id){throw 'Filename edit ownership changed'}
    if([VibeRaPatternObservation]::SendMessageText($edit,12,[IntPtr]::Zero,$path) -eq [IntPtr]::Zero){throw 'Native filename control rejected text'}
    $record.Add('Targeted owned native filename-control WM_SETTEXT, followed by actual OS Enter; clipboard unchanged.')
    Key 13
    for($i=0;$i -lt 30;$i++){
     Start-Sleep -Milliseconds 100
     $fileState=Native-State ('groove-file-'+$i)
     if(-not $fileState.quantizerPreview.fileDialogPending){break}
    }
    if($fileState.quantizerPreview.fileDialogPending){throw 'Native groove file choice did not finish'}
    [VibeRaPatternObservation]::SetForegroundWindow($dialog)|Out-Null
   }
   Dialog-Click 'quantizer.capture'
   $captured=Native-State 'groove-captured'
   if($captured.quantizerPreview.groovePoints -lt 1){throw 'Native Capture did not create a template'}
   Dialog-Click 'quantizer.groove';Key 36;Key 40;Key 40;Key 40;Key 13
   $preset=Native-State 'groove-preset'
   if($preset.quantizerPreview.grooveName -ne 'VibeRa light swing' -or $preset.quantizerPreview.completionState -ne 'pending'){throw 'Native preset selection did not stay in preview'}
   if($preset.quantizerWindow.controls.'quantizer.grid'.enabled){throw 'Uniform grid control incorrectly active during a groove'}
   Dialog-Click 'quantizer.velocity' $true
   Control-Key 65;Key 49;Key 48;Key 48;Key 13
   $level=Native-State 'groove-level'
   if($level.quantizerPreview.velocityStrength -ne 1 -or $level.quantizerPreview.completionState -ne 'pending'){throw 'Native velocity field did not update preview'}
   $savedGroove=Join-Path $Output 'native-saved.vgroove'
   File-Choice $true $savedGroove
   $savedModel=Get-Content -LiteralPath $savedGroove -Raw|ConvertFrom-Json
   if($savedModel.format -ne 'VibeRa.Groove' -or $savedModel.points.Count -ne 16){throw 'Native groove save did not persist the selected template'}
   $savedModel.points[0].velocity=0
   $restGroove=Join-Path $Output 'native-rests.vgroove'
   [IO.File]::WriteAllText($restGroove,($savedModel|ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
   File-Choice $false $restGroove
   $restState=Native-State 'groove-rest-import'
   if(@($restState.notes).Count -ge @($before.notes).Count){throw 'Imported zero-velocity rests did not remove matching score notes'}
   Dialog-Click 'quantizer.rests'
   $restored=Native-State 'groove-rest-disabled'
   if(@($restored.notes).Count -ne @($before.notes).Count -or $restored.quantizerPreview.zeroVelocityDeletes){throw 'Native rest toggle did not restore original note identities'}
   Capture $dialog 'native-groove-dialog.png'
   Capture $h 'native-groove-guides.png'
   Dialog-Click 'quantizer.reset'
   $reset=Native-State 'groove-reset'
   if($reset.quantizerPreview.groovePoints -ne 0 -or $reset.quantizerPreview.velocityStrength -ne 0 -or $reset.quantizerGuide){throw 'Native Reset did not clear groove/velocity/guides'}
   $record.Add('PASS native Capture/preset/velocity editing and actual Save/Load file pickers, rest deletion/restoration, Reset; MCP read-only observation. Filename control used targeted native WM_SETTEXT, not human typing.')
  }
  $pending=Native-State 'quantizer-pending'
  if(-not $pending.gestureActive -or $pending.quantizerPreview.completionState -ne 'pending' -or $pending.quantizerPreview.mode -ne 'duration'){throw 'Native Quantizer preview state incorrect'}
  Capture $dialog 'native-quantizer-dialog.png'
  Key 27
  [VibeRaPatternObservation]::SetForegroundWindow($h)|Out-Null
  $cancelled=Native-State 'quantizer-cancelled'
  if((Score $cancelled) -ne $original -or $cancelled.gestureActive -or $cancelled.quantizerPreview.completionState -ne 'cancelled'){throw 'Native Escape did not restore exact score'}
  $dialog=Open-Quantizer
  Key 13
  [VibeRaPatternObservation]::SetForegroundWindow($h)|Out-Null
  $accepted=Native-State 'quantizer-accepted'
  if($accepted.gestureActive -or $accepted.quantizerPreview.completionState -ne 'completed' -or (Score $accepted) -eq $original){throw 'Native Enter did not commit Quantizer'}
  $changed=Score $accepted
  Control-Key 90
  $undone=Native-State 'quantizer-undone'
  if((Score $undone) -ne $original){throw 'Native Ctrl+Z did not restore exact baseline'}
  Control-Key 89
  $redone=Native-State 'quantizer-redone'
  if((Score $redone) -ne $changed){throw 'Native Ctrl+Y did not restore accepted timing'}
  Control-Key 83
  Start-Sleep -Milliseconds 300
  $saved=Native-State 'quantizer-saved'
  if($saved.dirty){throw 'Native Ctrl+S did not finish saving'}
  Capture $h 'native-quantizer-workspace.png'
  [VibeRaPatternObservation]::SetWindowPos($h,[IntPtr]::Zero,0,0,3840,2088,16)|Out-Null
  Start-Sleep -Milliseconds 150
  Capture $h 'native-quantizer-4k.png'
  $record.Add('PASS actual OS Rack-indicator/Piano-toolbar focus and Shift+Q/Ctrl+Q distinct quick actions with exact Undo; Alt+Q opens live Quantizer; Escape restores exact source; Enter commits; Ctrl+Z/Y exact history; Ctrl+S actual save. MCP read-only observation only. No listening/hardware claim.')
  return
 }
  function Graph-Drag([double]$x1,[double]$y1,[double]$x2,[double]$y2,[string]$button='left',[int]$modifier=0,[bool]$Cancel=$false){
   Assert-ActiveOwned
   $origin=New-Object VibeRaPatternObservation+Point
   [VibeRaPatternObservation]::ClientToScreen($h,[ref]$origin)|Out-Null
   $downFlag=if($button -eq 'right'){[uint32]8}else{[uint32]2}
   $upFlag=if($button -eq 'right'){[uint32]16}else{[uint32]4}
   $pressedModifier=$false
   $lastPoint=New-Object VibeRaPatternObservation+Point
   try{
    if($modifier){[VibeRaPatternObservation]::keybd_event([byte]$modifier,(Scan-Code ([byte]$modifier)),0,[UIntPtr]::Zero);$pressedModifier=$true;Start-Sleep -Milliseconds 40}
    for($step=0;$step -le 8;$step++){
     $foreground=[uint32]0
     [VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::GetForegroundWindow(),[ref]$foreground)|Out-Null
     if($foreground -ne $app.Id){throw 'Foreground changed during owned graph drag; input paused'}
     if($step -gt 0){
      $current=New-Object VibeRaPatternObservation+Point
      [VibeRaPatternObservation]::GetCursorPos([ref]$current)|Out-Null
      if($current.x -ne $lastPoint.x -or $current.y -ne $lastPoint.y){throw 'User pointer moved during graph drag; input paused'}
     }
     foreach($key in (@(13,16,17,18,27,91,92)+@(32..90))){
      if($key -ne $modifier -and ([VibeRaPatternObservation]::GetAsyncKeyState($key) -band 32768) -ne 0){throw 'User keyboard input during graph drag; input paused'}
     }
     $amount=$step/8.0
     $lastPoint.x=$origin.x+[int](($x1+($x2-$x1)*$amount)*$scale)
     $lastPoint.y=$origin.y+[int](($y1+($y2-$y1)*$amount+36)*$scale)
     $hitPid=[uint32]0
     [VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::WindowFromPoint($lastPoint),[ref]$hitPid)|Out-Null
     if($hitPid -ne $app.Id){throw 'Owned graph drag point obscured; input paused'}
     [VibeRaPatternObservation]::SetCursorPos($lastPoint.x,$lastPoint.y)|Out-Null
     if($step -eq 0){[VibeRaPatternObservation]::mouse_event($downFlag,0,0,0,[UIntPtr]::Zero);$script:ownedPointerDown=$true}
     Start-Sleep -Milliseconds 35
    }
    if($Cancel){
     [VibeRaPatternObservation]::keybd_event(27,(Scan-Code 27),0,[UIntPtr]::Zero)
     [VibeRaPatternObservation]::keybd_event(27,(Scan-Code 27),2,[UIntPtr]::Zero)
     Start-Sleep -Milliseconds 100
    }
   }finally{
    if($script:ownedPointerDown){[VibeRaPatternObservation]::mouse_event($upFlag,0,0,0,[UIntPtr]::Zero);$script:ownedPointerDown=$false}
    if($pressedModifier){[VibeRaPatternObservation]::keybd_event([byte]$modifier,(Scan-Code ([byte]$modifier)),2,[UIntPtr]::Zero)}
   }
   Start-Sleep -Milliseconds 180
  }
 if($Swing){
  Capture $h 'native-swing-before.png'
  $view=Native-State 'swing-initial'
  $box=$view.rack.swingControl
  if(-not $box.visible -or $view.swing -ne 0){throw 'Expected visible global Swing and known zero baseline'}
  Key 117
  Control-Key 83
  $before=[xml](Get-Content -LiteralPath $Project -Raw)
  $x=[double]$box.x+[double]$box.width-13
  $y=[double]$box.y+[double]$box.height/2
  Graph-Drag $x $y $x ($y-32)
  Key 117
  Control-Key 83
  $changed=[xml](Get-Content -LiteralPath $Project -Raw)
  $amount=[double]$changed.VIBERA.swing
  if($amount -le 0 -or $amount -gt 1){throw 'Native Swing drag did not change the real saved value'}
  if($changed.VIBERA.PATTERNS.OuterXml -ne $before.VIBERA.PATTERNS.OuterXml -or $changed.VIBERA.CHANNELS.OuterXml -ne $before.VIBERA.CHANNELS.OuterXml){throw 'Native Swing rewrote source notes or Channel settings'}
  $view=Native-State 'swing-edited'
  if([Math]::Abs([double]$view.swing-$amount) -gt 0.000000001){throw 'Native saved Swing and semantic readback disagree'}
  Control-Key 90
  Control-Key 83
  $undone=[xml](Get-Content -LiteralPath $Project -Raw)
  if([double]$undone.VIBERA.swing -ne 0){throw 'Native one-drag Undo did not restore zero'}
  Control-Key 89
  Control-Key 83
  $redone=[xml](Get-Content -LiteralPath $Project -Raw)
  if([double]$redone.VIBERA.swing -ne $amount){throw 'Native Swing Redo is not exact'}
  Graph-Drag $x $y $x ($y-15) 'left' 0 $true
  Key 117
  Control-Key 83
  $cancelled=[xml](Get-Content -LiteralPath $Project -Raw)
  if([double]$cancelled.VIBERA.swing -ne $amount){throw 'Native Escape did not cancel Swing drag exactly'}
  if($SwingDialogs){
   $rack=@($view.editors|Where-Object {$_.index -eq 0})[0]
   $rackBox=$rack.contentBounds
   Click ([double]$rackBox[0]+12) ([double]$rackBox[1]+60)
   $view=Native-State 'swing-selected'
   $target=[long]$view.selectedChannel
   $previousMix=[double]@($view.channels|Where-Object {$_.id -eq $target})[0].swingMix
   function Click-Owned-Popup([IntPtr]$window,[int]$px,[int]$py){
    Assert-ActiveOwned
    $origin=New-Object VibeRaPatternObservation+Point
    [VibeRaPatternObservation]::ClientToScreen($window,[ref]$origin)|Out-Null
    $point=New-Object VibeRaPatternObservation+Point
    $point.x=$origin.x+$px;$point.y=$origin.y+$py
    $targetPid=[uint32]0
    [VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::WindowFromPoint($point),[ref]$targetPid)|Out-Null
    if($targetPid -ne $app.Id){throw 'Owned popup obscured; no click'}
    [VibeRaPatternObservation]::SetCursorPos($point.x,$point.y)|Out-Null
    Start-Sleep -Milliseconds 200
    Assert-ActiveOwned
    $actualPoint=New-Object VibeRaPatternObservation+Point
    [VibeRaPatternObservation]::GetCursorPos([ref]$actualPoint)|Out-Null
    if($actualPoint.x -ne $point.x -or $actualPoint.y -ne $point.y){throw 'User pointer moved before popup click; input paused'}
    if([VibeRaPatternObservation]::WindowFromPoint($actualPoint) -ne $window){throw 'Owned popup click target changed; no input'}
    [VibeRaPatternObservation]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
    try{Start-Sleep -Milliseconds 40}finally{[VibeRaPatternObservation]::mouse_event(4,0,0,0,[UIntPtr]::Zero)}
    Start-Sleep -Milliseconds 180
   }
   function Open-Swing-Custom {
    $menu=@($rack.controls|Where-Object {$_.name -eq 'Menu'})[0]
    Click ([double]$rackBox[0]+[double]$menu.x+20) ([double]$rackBox[1]+[double]$menu.y+12)
    $popup=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,'')
    if($popup -ne [IntPtr]::Zero){Capture $popup 'native-swing-menu.png'}
    if($SwingMenuProbe){Key 27;return}
    $menuRect=New-Object VibeRaPatternObservation+Rect
    [VibeRaPatternObservation]::GetClientRect($popup,[ref]$menuRect)|Out-Null
    # Observed popup has Mix immediately before the final truncation row.
    Click-Owned-Popup $popup 160 ($menuRect.bottom-[int](34*$scale))
    $submenu=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,'')
    Capture $submenu 'native-swing-mix-menu.png'
    $subRect=New-Object VibeRaPatternObservation+Rect
    [VibeRaPatternObservation]::GetClientRect($submenu,[ref]$subRect)|Out-Null
    if($subRect.bottom -gt 400){throw 'Mix submenu did not open; no guessed mutation'}
    Click-Owned-Popup $submenu 65 ($subRect.bottom-[int](11*$scale))
    Start-Sleep -Milliseconds 400
    $record.Add([VibeRaPatternObservation]::DescribeOwned([uint32]$app.Id))
    $script:currentSwingDialog=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,'Channel Swing Mix')
    if($script:currentSwingDialog -eq [IntPtr]::Zero){throw 'Actual custom dialog missing'}
    $dialogPid=[uint32]0
    [VibeRaPatternObservation]::GetWindowThreadProcessId($script:currentSwingDialog,[ref]$dialogPid)|Out-Null
    if($dialogPid -ne $app.Id){throw 'Refuse raising an unowned dialog'}
    [VibeRaPatternObservation]::SetWindowPos($script:currentSwingDialog,[IntPtr](-1),0,0,0,0,19)|Out-Null
    [VibeRaPatternObservation]::SetForegroundWindow($script:currentSwingDialog)|Out-Null
    Start-Sleep -Milliseconds 120
    Click-Owned-Popup $script:currentSwingDialog 150 205
    $record.Add('Raised/focused only owned modal HWND above the test-only topmost main window; textbox/Apply use inspected dialog geometry.')
    Start-Sleep -Milliseconds 180
   }
   Open-Swing-Custom
   if($SwingMenuProbe){$record.Add('OBSERVATION ONLY: captured actual owned Swing menu to resolve native submenu targeting; no dialog qualification.');return}
   $dialog=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,'Channel Swing Mix')
   if($dialog -eq [IntPtr]::Zero){$dialog=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,'')}
   if($dialog -ne [IntPtr]::Zero){Capture $dialog 'native-swing-custom-dialog.png'}else{Capture $h 'native-swing-custom-dialog.png'}
   Control-Key 65
   Key 66
   Key 65
   Key 68
   Click-Owned-Popup $script:currentSwingDialog 187 276
   Start-Sleep -Milliseconds 250
   $invalid=Native-State 'swing-invalid-custom'
   if([double]@($invalid.channels|Where-Object {$_.id -eq $target})[0].swingMix -ne $previousMix -or $invalid.status -notlike '*Swing percent must be*'){throw 'Native invalid Swing custom input did not refuse atomically'}
   Open-Swing-Custom
   Control-Key 65
   Key 55
   Key 53
   Click-Owned-Popup $script:currentSwingDialog 187 276
   Start-Sleep -Milliseconds 250
   $custom=Native-State 'swing-custom-applied'
   if([double]@($custom.channels|Where-Object {$_.id -eq $target})[0].swingMix -ne .75){throw 'Native custom 75 percent did not reach selected Channel'}
   Key 117
   Control-Key 83
   $customSaved=[xml](Get-Content -LiteralPath $Project -Raw)
   if([double]@($customSaved.VIBERA.CHANNELS.CHANNEL|Where-Object {[long]$_.id -eq $target})[0].swingMix -ne .75){throw 'Native custom Swing Mix was not saved'}
   Control-Key 90
   Control-Key 83
   $customUndo=[xml](Get-Content -LiteralPath $Project -Raw)
   if([double]@($customUndo.VIBERA.CHANNELS.CHANNEL|Where-Object {[long]$_.id -eq $target})[0].swingMix -ne $previousMix){throw 'Native custom Swing Mix Undo not exact'}
   Control-Key 89
   Control-Key 83
   $customRedo=[xml](Get-Content -LiteralPath $Project -Raw)
   if([double]@($customRedo.VIBERA.CHANNELS.CHANNEL|Where-Object {[long]$_.id -eq $target})[0].swingMix -ne .75){throw 'Native custom Swing Mix Redo not exact'}
   $record.Add('PASS native Rack selection/Menu/Mix Custom dialog: invalid text refused, 75 percent applied/saved, exact one Undo/Redo. No semantic mutation substituted.')
  }
  Control-Key 75
  Capture $h 'native-swing-final.png'
  Assert-ActiveOwned
  [VibeRaPatternObservation]::SetWindowPos($h,[IntPtr]::Zero,0,0,3840,2088,16)|Out-Null
  Start-Sleep -Milliseconds 200
  Capture $h 'native-swing-4k.png'
  $record.Add("PASS actual guarded native Swing drag/value readback/Save/one Undo/Redo/Escape; source score/Channels retained. Swing=$amount. Same-instance MCP read-only observation; no hardware/listening.")
  return
 }
 if($StepVelocity){
  Capture $h 'native-step-velocity-before.png'
  if([Math]::Abs($scale-1.5) -gt .001){throw 'Velocity desktop test requires inspected144DPI layout'}
  $view=Native-State 'velocity-initial'
  $rackEditor=@($view.editors | Where-Object {$_.index -eq 0})[0]
  $bounds=$rackEditor.contentBounds
  Click ([double]$bounds[0]+12) ([double]$bounds[1]+60)
  Key 117
  Control-Key 75
  $view=Native-State 'velocity-open'
  if(-not $view.rack.graph.visible -or -not $view.rack.graph.rendered -or [int]$view.scope -ne 1){throw 'Actual native Ctrl+K did not open graph'}
  $target=[long]$view.selectedChannel
  Control-Key 83
  $before=[xml](Get-Content -LiteralPath $Project -Raw)
  $pattern=[long]$view.selectedPattern
  function Step-Notes($xml){
   $part=@($xml.VIBERA.PATTERNS.PATTERN | Where-Object {[long]$_.id -eq $pattern})[0]
   return @($part.NOTE | Where-Object {[long]$_.channel -eq $target -and [int]$_.step -eq 1 -and [double]$_.beat -lt 1})
  }
  if((Step-Notes $before).Count -ne 4){throw 'Velocity native fixture must have four active first-beat steps'}
  $chart=$view.rack.graph.chartBoundsInStudio
  $width=[double]$view.rack.graph.columnWidth
  $first=[int]$view.rack.graph.firstStep
  if($first -ne 0 -or $width -lt 10 -or $chart[3] -lt 40){throw 'Unusable or unexpected native graph viewport'}
  $x0=[double]$chart[0]+.5*$width
  $x3=[double]$chart[0]+3.5*$width
  function Graph-Y([double]$value){return [double]$chart[1]+[double]$chart[3]*(1-$value)}

  Graph-Drag $x0 (Graph-Y .25) $x0 (Graph-Y .25)
  Control-Key 83
  $single=[xml](Get-Content -LiteralPath $Project -Raw)
  if([Math]::Abs([double](Step-Notes $single)[0].velocity-.25) -gt .03){throw 'Native single-column velocity edit missed its value'}
  $before=$single
  Graph-Drag $x0 (Graph-Y .25) $x3 (Graph-Y .75)
  Control-Key 83
  $painted=[xml](Get-Content -LiteralPath $Project -Raw)
  $paintedNotes=Step-Notes $painted
  $trace=@($paintedNotes|ForEach-Object {[double]$_.velocity})
  $record.Add('Native freehand painted values: '+($trace -join ', '))
  if($trace[0] -lt .2 -or $trace[0] -gt .45 -or [Math]::Abs($trace[3]-.75) -gt .03 -or $trace[1] -le $trace[0] -or $trace[2] -le $trace[1]){throw 'Actual OS freehand paint missed expected increasing dynamics'}
  $view=Native-State 'velocity-painted'
  if($view.rack.graph.gestureActive){throw 'Native graph mouse-up failed to complete transaction'}
  Capture $h 'native-step-velocity-painted.png'
  Control-Key 90;Control-Key 83
  $undone=[xml](Get-Content -LiteralPath $Project -Raw)
  if($undone.VIBERA.PATTERNS.OuterXml -ne $before.VIBERA.PATTERNS.OuterXml){throw 'Native graph drag did not have one exact Undo'}
  Control-Key 89;Control-Key 83
  $redone=[xml](Get-Content -LiteralPath $Project -Raw)
  if($redone.VIBERA.PATTERNS.OuterXml -ne $painted.VIBERA.PATTERNS.OuterXml){throw 'Native graph Redo mismatch'}
  Graph-Drag $x0 (Graph-Y .2) $x3 (Graph-Y .8) 'right'
  Control-Key 83
  $ramped=[xml](Get-Content -LiteralPath $Project -Raw);$rampNotes=Step-Notes $ramped
  if([Math]::Abs([double]$rampNotes[1].velocity-.4) -gt .04 -or [Math]::Abs([double]$rampNotes[2].velocity-.6) -gt .04){throw 'Native right ramp interpolation failed'}
  Control-Key 90;Control-Key 83
  $restored=[xml](Get-Content -LiteralPath $Project -Raw)
  if($restored.VIBERA.PATTERNS.OuterXml -ne $painted.VIBERA.PATTERNS.OuterXml){throw 'Native ramp Undo mismatch'}
  Graph-Drag $x0 (Graph-Y .5) $x3 (Graph-Y .5) 'left' 18
  Control-Key 83
  $reset=[xml](Get-Content -LiteralPath $Project -Raw)
  if(@(Step-Notes $reset | Where-Object {[Math]::Abs([double]$_.velocity-.8) -gt .000001}).Count){throw 'Native Alt reset missed step default'}
  Control-Key 90;Control-Key 83
  Graph-Drag $x0 (Graph-Y .25) $x0 (Graph-Y .5) 'left' 17
  Control-Key 83
  $scaled=[xml](Get-Content -LiteralPath $Project -Raw);$scaledNotes=Step-Notes $scaled
  if([double]$scaledNotes[0].velocity -lt .45 -or [double]$scaledNotes[3].velocity -lt .95){throw 'Native Ctrl scale failed or did not clip'}
  Control-Key 90;Control-Key 83
  Graph-Drag $x0 (Graph-Y .1) $x3 (Graph-Y .2) 'left' 0 $true
  Control-Key 83
  $cancelled=[xml](Get-Content -LiteralPath $Project -Raw)
  if($cancelled.VIBERA.PATTERNS.OuterXml -ne $painted.VIBERA.PATTERNS.OuterXml){throw 'Native Escape failed exact cancellation'}
  Control-Key 90;Control-Key 83
  $history=[xml](Get-Content -LiteralPath $Project -Raw)
  if($history.VIBERA.PATTERNS.OuterXml -ne $before.VIBERA.PATTERNS.OuterXml){throw 'Cancelled native drag created an Undo entry'}
  Control-Key 89;Control-Key 83
  $view=Native-State 'velocity-final'
  $rackEditor=@($view.editors|Where-Object {$_.index -eq 0})[0]
  $graphButton=@($rackEditor.controls|Where-Object {$_.name -eq 'Graph editor'})[0]
  if(-not $graphButton){throw 'Graph toggle discovery missing'}
  Click ([double]$rackEditor.contentBounds[0]+[double]$graphButton.x+[double]$graphButton.width/2) ([double]$rackEditor.contentBounds[1]+[double]$graphButton.y+[double]$graphButton.height/2)
  $hidden=Native-State 'velocity-hidden'
  if($hidden.rack.graph.visible){throw 'Actual native graph button did not hide view'}
  Control-Key 75
  Control-Key 83
  $final=[xml](Get-Content -LiteralPath $Project -Raw)
  if($final.VIBERA.PATTERNS.OuterXml -ne $painted.VIBERA.PATTERNS.OuterXml -or $final.VIBERA.CHANNELS.OuterXml -ne $before.VIBERA.CHANNELS.OuterXml -or $final.VIBERA.PLACEMENTS.OuterXml -ne $before.VIBERA.PLACEMENTS.OuterXml){throw 'Native graph workflow changed unrelated state'}
  Capture $h 'native-step-velocity-final.png'
  Assert-ActiveOwned
  [VibeRaPatternObservation]::SetWindowPos($h,[IntPtr]::Zero,0,0,3840,2088,16)|Out-Null
  Start-Sleep -Milliseconds 200
  Capture $h 'native-step-velocity-4k.png'
  $record.Add('PASS actual guarded native Ctrl+K/button, left paint, right ramp, Ctrl scale, Alt reset, Escape and exact Save/Undo/Redo; stable musical data/routing retained. Same-instance MCP was read-only observation. No physical MIDI, audio or listening claim.')
  return
 }
 if($ChannelManagement){
  Capture $h 'native-channel-management-before.png'
  if([Math]::Abs($scale-1.5) -gt .001){throw 'Channel management input requires inspected144DPI layout'}
  Control-Key 83
  $before=[xml](Get-Content -LiteralPath $Project -Raw)
  if([int]$before.VIBERA.version -ne 12 -or @($before.VIBERA.CHANNELS.CHANNEL).Count -ne 4){throw 'Unexpected disposable management fixture'}
  $target=[long]@($before.VIBERA.CHANNELS.CHANNEL)[-1].id
  $sourceNotes=@(@($before.VIBERA.PATTERNS.PATTERN)[0].NOTE | Where-Object {[long]$_.channel -eq $target})
  if(-not $sourceNotes.Count){throw 'Native Copy/Clone/Paste requires score on last loaded Channel'}
  # This visible test-mode window skips normal startup keyboard focus. Focus
  # the Rack's painted labels using real mouse input, away from notes/controls.
  Click 1100 430
  Key 117
  Key 117
  Native-State 'after-f6'|Out-Null
  Capture $h 'native-channel-management-focused.png'
  Control-Key 67
  Alt-Key 67
  Native-State 'after-alt-c'|Out-Null
  Capture $h 'native-channel-management-shortcut.png'
  Control-Key 83
  $shortcutClone=[xml](Get-Content -LiteralPath $Project -Raw)
  if(@($shortcutClone.VIBERA.CHANNELS.CHANNEL).Count -ne 5){throw 'Actual Alt+C did not clone selected Channel'}
  Control-Key 90
  Control-Key 83
  $shortcutUndo=[xml](Get-Content -LiteralPath $Project -Raw)
  if($shortcutUndo.OuterXml -ne $before.OuterXml){throw 'Native shortcut clone Undo was not exact'}
  Right-Click 900 568
  $popup=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,'')
  if($popup -eq [IntPtr]::Zero){throw 'Channel context menu was not opened'}
  Key 36
  for($item=0;$item -lt 6;$item++){Key 40}
  Capture $popup 'native-channel-management-menu.png'
  Key 13
  Control-Key 83
  $cloned=[xml](Get-Content -LiteralPath $Project -Raw)
  if(@($cloned.VIBERA.CHANNELS.CHANNEL).Count -ne 5){Capture $h 'native-channel-management-clone-failure.png';throw 'Actual context Clone did not create one Channel'}
  $clone=[long]@($cloned.VIBERA.CHANNELS.CHANNEL)[-1].id
  if($clone -eq $target -or @($cloned.VIBERA.CHANNELS.CHANNEL)[-2].id -ne $target){throw 'Native clone was not adjacent with an independent ID'}
  if($cloned.VIBERA.PATTERNS.OuterXml -ne $before.VIBERA.PATTERNS.OuterXml){throw 'Clone unexpectedly copied or changed score'}
  Control-Key 86
  Control-Key 83
  $pasted=[xml](Get-Content -LiteralPath $Project -Raw)
  $copyNotes=@(@($pasted.VIBERA.PATTERNS.PATTERN)[0].NOTE | Where-Object {[long]$_.channel -eq $clone})
  if($copyNotes.Count -ne $sourceNotes.Count){throw 'Actual Ctrl+V did not paste score into the new clone'}
  for($note=0;$note -lt $sourceNotes.Count;$note++){
   foreach($property in @('beat','length','pitch','velocity','step')){
    if($copyNotes[$note].GetAttribute($property) -ne $sourceNotes[$note].GetAttribute($property)){throw 'Pasted musical attributes differ'}
   }
  }
  Capture $h 'native-channel-management-pasted.png'
  Control-Key 90
  Control-Key 90
  Control-Key 83
  $undone=[xml](Get-Content -LiteralPath $Project -Raw)
  if($undone.OuterXml -ne $before.OuterXml){throw 'Two native Undo actions did not restore exact baseline'}
  Control-Key 89
  Control-Key 89
  Control-Key 83
  $redone=[xml](Get-Content -LiteralPath $Project -Raw)
  if($redone.OuterXml -ne $pasted.OuterXml){throw 'Native Redo did not restore clone/paste exactly'}
  $view=Native-State 'after-redo'
  if([int]$view.rack.viewport.firstRow -ne 0){throw 'Clone selector coordinates require the inspected top Rack viewport'}
  Click 842 594
  $view=Native-State 'after-clone-select'
  if([long]$view.selectedChannel -ne $clone -or @($view.rack.selectedChannels).Count -ne 1){throw 'Native selector did not select the intended clone; no reorder sent'}
  Alt-Key 38
  Control-Key 83
  $moved=[xml](Get-Content -LiteralPath $Project -Raw)
  if([long]@($moved.VIBERA.CHANNELS.CHANNEL)[-2].id -ne $clone -or $moved.VIBERA.PATTERNS.OuterXml -ne $pasted.VIBERA.PATTERNS.OuterXml){throw 'Actual Alt+Up did not reorder only the selected clone'}
  Control-Key 90
  Alt-Key 46
  Control-Key 83
  $deleted=[xml](Get-Content -LiteralPath $Project -Raw)
  if($deleted.VIBERA.CHANNELS.OuterXml -ne $before.VIBERA.CHANNELS.OuterXml -or $deleted.VIBERA.PATTERNS.OuterXml -ne $before.VIBERA.PATTERNS.OuterXml){throw 'Actual Alt+Delete removed unrelated data or left clone score'}
  Control-Key 90
  Control-Key 83
  $restored=[xml](Get-Content -LiteralPath $Project -Raw)
  if($restored.OuterXml -ne $pasted.OuterXml){throw 'Delete Undo did not restore exact clone/score'}
  Capture $h 'native-channel-management-restored.png'
  $record.Add("PASS: actual guarded native F6 scope, Ctrl+C/Alt+C/Ctrl+V, Alt+Up/Alt+Delete, Ctrl+S/Z/Y; independent settings-only clone, explicit score transfer, exact history/order/deletion and preserved unrelated project data. PID $($app.Id), creation $creation. No physical MIDI/audio/listening claim.")
  return
 }
 if($ChannelSolo){
  Capture $h 'native-channel-solo-before.png'
  if([Math]::Abs($scale-1.5) -gt .001){throw 'Solo click coordinates require inspected144DPI layout; no musical input sent'}
  Control-Key 83
  $before=[xml](Get-Content -LiteralPath $Project -Raw)
  if([int]$before.VIBERA.version -ne 12 -or @($before.VIBERA.CHANNELS.CHANNEL).Count -ne 4 -or [long]$before.VIBERA.CHANNEL_SOLO.channel -ne 0){throw 'Unexpected disposable solo fixture'}
  $target=[long]$before.VIBERA.CHANNELS.CHANNEL[0].id
  Right-Click 855 454
  $popup=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,'')
  if($popup -eq [IntPtr]::Zero){throw 'Mute-light right-click did not open owned menu'}
  Key 36
  Key 40
  Capture $popup 'native-channel-solo-menu.png'
  Key 13
  Control-Key 83
  $after=[xml](Get-Content -LiteralPath $Project -Raw)
  if([long]$after.VIBERA.CHANNEL_SOLO.channel -ne $target){Capture $h 'native-channel-solo-not-delivered.png';throw 'Actual mute-light menu did not solo the intended Channel'}
  foreach($channel in $after.VIBERA.CHANNELS.CHANNEL){
   $expected=if([long]$channel.id -eq $target){0}else{1}
   if([int]$channel.muted -ne $expected){throw 'Actual solo did not produce exact full-rack mute mask'}
  }
  if($after.VIBERA.PATTERNS.OuterXml -ne $before.VIBERA.PATTERNS.OuterXml -or $after.VIBERA.PLACEMENTS.OuterXml -ne $before.VIBERA.PLACEMENTS.OuterXml){throw 'Solo changed musical events or linked arrangement'}
  Capture $h 'native-channel-solo-after.png'
  Control-Key 90
  Control-Key 83
  $undone=[xml](Get-Content -LiteralPath $Project -Raw)
  if($undone.OuterXml -ne $before.OuterXml){throw 'Actual Ctrl+Z/Save did not restore exact solo baseline'}
  Control-Key 89
  Control-Key 83
  $redone=[xml](Get-Content -LiteralPath $Project -Raw)
  if($redone.OuterXml -ne $after.OuterXml){throw 'Actual Ctrl+Y/Save did not restore active solo/snapshot'}
  Right-Click 855 454
  Key 36
  Key 40
  Key 13
  Control-Key 83
  $restored=[xml](Get-Content -LiteralPath $Project -Raw)
  if($restored.OuterXml -ne $before.OuterXml){throw 'Actual menu Unsolo did not restore exact prior mute pattern'}
  Capture $h 'native-channel-solo-restored.png'
  $record.Add("PASS: actual guarded OS mute-light context Solo/Unsolo, Ctrl+S/Z/Y; full-rack mask, exact project baseline/Undo/Redo and unchanged musical events/links. PID $($app.Id), creation $creation, SHA256 $((Get-FileHash -LiteralPath $exe).Hash). No physical MIDI/audio/listening claim.")
  return
 }
 if($StepFill){
  Capture $h 'native-step-fill-before.png'
  if([Math]::Abs($scale-1.5) -gt .001){throw 'Musical click coordinates require the inspected144DPI layout; no musical input sent'}
  Control-Key 83
  $before=[xml](Get-Content -LiteralPath $Project -Raw)
  if([int]$before.VIBERA.version -ne 12 -or @($before.VIBERA.CHANNELS.CHANNEL).Count -ne 4 -or [double]@($before.VIBERA.PATTERNS.PATTERN)[0].length -ne 24){throw 'Unexpected disposable fill fixture'}
  $target=[long]$before.VIBERA.CHANNELS.CHANNEL[2].id
  $pattern=[long]@($before.VIBERA.PATTERNS.PATTERN)[0].id
  Right-Click 900 530
  $popup=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,'')
  if($popup -eq [IntPtr]::Zero){throw 'Channel right-click did not open owned native menu'}
  Capture $popup 'native-step-fill-menu.png'
  Key 36
  Key 40
  Key 40
  Key 40
  Capture $popup 'native-step-fill-highlight.png'
  Key 13
  Control-Key 83
  $after=[xml](Get-Content -LiteralPath $Project -Raw)
  $hits=@(@($after.VIBERA.PATTERNS.PATTERN)[0].NOTE | Where-Object {[long]$_.channel -eq $target -and [double]$_.beat -lt 24 -and [int]$_.step -eq 1})
  if($hits.Count -ne 48){Capture $h 'native-step-fill-not-delivered.png';throw 'Native Fill each2 did not produce48 in-window hits'}
  $beats=@($hits | ForEach-Object {[double]$_.beat} | Sort-Object)
  for($i=0;$i -lt 48;$i++){if($beats[$i] -ne $i*.5){throw 'Native fill rhythm mismatch'}}
  $unrelatedBefore=@(@($before.VIBERA.PATTERNS.PATTERN)[0].NOTE | Where-Object {[long]$_.channel -ne $target} | ForEach-Object {$_.OuterXml}) -join '|'
  $unrelatedAfter=@(@($after.VIBERA.PATTERNS.PATTERN)[0].NOTE | Where-Object {[long]$_.channel -ne $target} | ForEach-Object {$_.OuterXml}) -join '|'
  if($unrelatedBefore -ne $unrelatedAfter -or [long]@($after.VIBERA.PATTERNS.PATTERN)[0].id -ne $pattern -or [double]@($after.VIBERA.PATTERNS.PATTERN)[0].length -ne 24){throw 'Native fill changed unrelated score or Pattern identity/length'}
  Capture $h 'native-step-fill-after.png'
  Control-Key 90
  Control-Key 83
  $restored=[xml](Get-Content -LiteralPath $Project -Raw)
  if($restored.OuterXml -ne $before.OuterXml){throw 'Native fill Ctrl+Z/Save exact Undo failed'}
  Control-Key 89
  Control-Key 83
  $redone=[xml](Get-Content -LiteralPath $Project -Raw)
  if($redone.OuterXml -ne $after.OuterXml){throw 'Native fill Ctrl+Y/Save exact Redo failed'}
  Capture $h 'native-step-fill-redone.png'
  $record.Add("PASS: actual scoped OS Channel name right-click, native fill menu/Home/Down/Enter, Ctrl+S/Z/Y;48 hits over96 cells, exact Undo/Redo persistence and unrelated parts retained. PID $($app.Id), creation $creation, SHA256 $((Get-FileHash -LiteralPath $exe).Hash). No physical MIDI/audio/listening claim.")
  return
 }
 if($SplitByChannel){
  Click 430 56 # Establish actual selector keyboard focus after native caption activation.
  Key 27
  if($KeyboardSaveUndo){Control-Key 83}else{Click 109 16}
  $before=[xml](Get-Content -LiteralPath $Project -Raw)
  if([int]$before.VIBERA.version -ne 12){Capture $h 'native-save-not-delivered.png';throw 'Native Save action did not write the current-schema disposable project'}
  $active=@($before.VIBERA.PATTERNS.PATTERN[0].NOTE | ForEach-Object {$_.channel} | Sort-Object -Unique).Count
  if($active -lt 2){throw 'Native split fixture needs at least two active Channels in its first Pattern'}
 }
 if($Picker){
  Capture $h 'native-picker-before.png'
  # Current 1920x1080/144DPI workspace: coordinates are logical Studio units.
  Click 280 266
  Assert-ActiveOwned
  $ctrl=Scan-Code 17
  [VibeRaPatternObservation]::keybd_event(17,$ctrl,0,[UIntPtr]::Zero)
  try {
   $origin=New-Object VibeRaPatternObservation+Point;[VibeRaPatternObservation]::ClientToScreen($h,[ref]$origin)|Out-Null
   $point=New-Object VibeRaPatternObservation+Point;$point.x=$origin.x+[int](280*$scale);$point.y=$origin.y+[int]((320+36)*$scale)
   $targetPid=[uint32]0;[VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::WindowFromPoint($point),[ref]$targetPid)|Out-Null
   if($targetPid -ne $app.Id){throw 'Picker second-row target obscured'}
   [VibeRaPatternObservation]::SetCursorPos($point.x,$point.y)|Out-Null
   [VibeRaPatternObservation]::mouse_event(2,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 60
   [VibeRaPatternObservation]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
  }finally{[VibeRaPatternObservation]::keybd_event(17,$ctrl,2,[UIntPtr]::Zero)}
  Start-Sleep -Milliseconds 200
  Capture $h 'native-picker-multiselect.png'
  Click 280 266
  # Drag one actual ListBox row through JUCE's DragAndDropContainer.
  Assert-ActiveOwned
  [VibeRaPatternObservation]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
  try {
   for($i=1;$i -le 20;$i++){
    $owner=[uint32]0;[VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::GetForegroundWindow(),[ref]$owner)|Out-Null
    if($owner -ne $app.Id){throw 'Picker drag lost foreground ownership'}
    $x=280+(676-280)*$i/20.;$y=266+(265-266)*$i/20.
    [VibeRaPatternObservation]::SetCursorPos($origin.x+[int]($x*$scale),$origin.y+[int](($y+36)*$scale))|Out-Null
    Start-Sleep -Milliseconds 30
   }
  }finally{[VibeRaPatternObservation]::mouse_event(4,0,0,0,[UIntPtr]::Zero)}
  Start-Sleep -Milliseconds 350
  Capture $h 'native-picker-dropped.png'
  Control-Key 83
  $placed=[xml](Get-Content -LiteralPath $Project -Raw)
  $oldCount=@($before.VIBERA.PLACEMENTS.PLACEMENT).Count
  $newClips=@($placed.VIBERA.PLACEMENTS.PLACEMENT)
  if($newClips.Count -ne $oldCount+1){throw "Native Picker drag expected one linked placement; found delta $($newClips.Count-$oldCount)"}
  $clip=$newClips[-1]
  if($clip.pattern -ne $before.VIBERA.PATTERNS.PATTERN[0].id -or [int]$clip.lane -ne 2){throw 'Native Picker drag placed an incorrect source/lane'}
  Copy-Item -LiteralPath $Project -Destination (Join-Path $Output 'native-picker-result.vibra')
  Control-Key 90
  Control-Key 83
  $undone=[xml](Get-Content -LiteralPath $Project -Raw)
  if($undone.OuterXml -ne $before.OuterXml){throw 'Native Picker drag Undo did not exactly restore baseline'}
  Control-Key 89
  Control-Key 83
  $redone=[xml](Get-Content -LiteralPath $Project -Raw)
  if($redone.OuterXml -ne $placed.OuterXml){throw 'Native Picker drag Redo mismatch'}
  $record.Add('PASS: actual OS ListBox selection, row drag through JUCE to Playlist, linked source/lane, Ctrl+S, exact Ctrl+Z/Ctrl+Y round trip. Multi-select screenshot is observation only; bulk placement separately component-tested. No playback/listening/hardware claim.')
  return
 }
 Capture $h 'native-pattern-workspace.png'
 Click 528 56
 $popup=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,'')
 if($popup -eq [IntPtr]::Zero){throw 'Pattern menu click did not open an owned native popup'}
 Capture $popup 'native-pattern-menu.png'
 if($SplitByChannel){
  # This row was inspected in the actual owned-popup render at144DPI.
  # Coordinates are logical, independently scaled for this popup.
  Assert-ActiveOwned
  $origin=New-Object VibeRaPatternObservation+Point
  [VibeRaPatternObservation]::ClientToScreen($popup,[ref]$origin) | Out-Null
  $popupScale=[VibeRaPatternObservation]::GetDpiForWindow($popup)/96.0
  $point=New-Object VibeRaPatternObservation+Point
  $point.x=$origin.x+[int](100*$popupScale);$point.y=$origin.y+[int](155*$popupScale)
  $hitPid=[uint32]0
  [VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::WindowFromPoint($point),[ref]$hitPid) | Out-Null
  if($hitPid -ne $app.Id){throw 'Split menu row is obscured; no pointer input'}
  [VibeRaPatternObservation]::SetCursorPos($point.x,$point.y) | Out-Null
  [VibeRaPatternObservation]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [VibeRaPatternObservation]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
  Start-Sleep -Milliseconds 250
  Capture $h 'native-pattern-after-menu.png'
  if($KeyboardSaveUndo){Control-Key 83}else{Click 109 16}
  $split=[xml](Get-Content -LiteralPath $Project -Raw)
  if(@($split.VIBERA.PATTERNS.PATTERN).Count -ne @($before.VIBERA.PATTERNS.PATTERN).Count+$active-1 -or
     $split.VIBERA.PATTERNS.PATTERN[0].id -ne $before.VIBERA.PATTERNS.PATTERN[0].id){throw 'Native menu selection did not persist split Pattern parts'}
  Capture $h 'native-pattern-split.png'
  Copy-Item -LiteralPath $Project -Destination (Join-Path $Output 'native-split-result.vibra')
  if($KeyboardSaveUndo){Control-Key 90;Control-Key 83}else{Click 241 16;Click 109 16}
  $restored=[xml](Get-Content -LiteralPath $Project -Raw)
  if($restored.OuterXml -ne $before.OuterXml){throw 'Native Undo/Save buttons did not exactly restore the source project'}
  if($KeyboardSaveUndo){
   if($RedoShortcut -eq 'CtrlY'){Control-Key 89}else{Control-Key 90 -Shift}
   Control-Key 83
   $redone=[xml](Get-Content -LiteralPath $Project -Raw)
   if($redone.OuterXml -ne $split.OuterXml){
    Capture $h 'native-redo-not-delivered.png'
    # Save captures state asynchronously; wait for completion without reinjecting keys.
    $until=[DateTime]::UtcNow.AddSeconds(3)
    while($redone.OuterXml -ne $split.OuterXml -and [DateTime]::UtcNow -lt $until){
     Start-Sleep -Milliseconds 100
     $redone=[xml](Get-Content -LiteralPath $Project -Raw)
    }
    if($redone.OuterXml -ne $split.OuterXml){
     Control-Key 89
     Control-Key 83
     $alternate=[xml](Get-Content -LiteralPath $Project -Raw)
     $record.Add("Diagnostic alternative Ctrl+Y/Save exact split restoration: $($alternate.OuterXml -eq $split.OuterXml)")
     throw "Native $RedoShortcut/Save did not restore exact split parts; alternate result is diagnostic only"
    }
   }
   Capture $h 'native-pattern-keyboard-redo.png'
   Control-Key 90
   Control-Key 83
   $restored=[xml](Get-Content -LiteralPath $Project -Raw)
   if($restored.OuterXml -ne $before.OuterXml){throw 'Second native Ctrl+Z/Save did not restore the exact baseline'}
   $record.Add("PASS: native $RedoShortcut/Save restores exact split state; subsequent Ctrl+Z/Save restores exact baseline.")
  }
  $method=if($KeyboardSaveUndo){'Ctrl+S/Z'}else{'Save/Undo toolbar buttons; no Ctrl+S/Z qualification'}
  $record.Add("PASS: actual OS menu selection split the Pattern; Save persisted parts; Undo/Save restored the exact baseline using $method. Disposable built-in project only.")
 }else{Key 27}
 if($Transpose){
  Click 528 56
  $popup=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,'')
  if($popup -eq [IntPtr]::Zero){throw 'Transpose Pattern menu did not open'}
  Capture $popup 'native-transpose-menu.png'
  # Split remains the previous row; Transpose is the next actual menu item.
  Assert-ActiveOwned
  $origin=New-Object VibeRaPatternObservation+Point
  [VibeRaPatternObservation]::ClientToScreen($popup,[ref]$origin) | Out-Null
  $popupScale=[VibeRaPatternObservation]::GetDpiForWindow($popup)/96.0
  $point=New-Object VibeRaPatternObservation+Point
  $point.x=$origin.x+[int](100*$popupScale);$point.y=$origin.y+[int](178*$popupScale)
  $hitPid=[uint32]0
  [VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::WindowFromPoint($point),[ref]$hitPid) | Out-Null
  if($hitPid -ne $app.Id){throw 'Transpose menu row obscured; no input'}
  [VibeRaPatternObservation]::SetCursorPos($point.x,$point.y) | Out-Null
  [VibeRaPatternObservation]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [VibeRaPatternObservation]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
  Start-Sleep -Milliseconds 180
  $dialog=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,'Transpose Pattern')
  if($dialog -eq [IntPtr]::Zero){
   $other=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,'')
   if($other -ne [IntPtr]::Zero){Capture $other 'native-transpose-unexpected-window.png'}
   throw 'Native Transpose menu selection did not open the real dialog'
  }
  Capture $dialog 'native-transpose-dialog.png'
  # The harness raised the main HWND over an occluding editor. Raise only its
  # owned modal too, then focus the actual editor before injecting text.
  $dialogOwner=[uint32]0
  [VibeRaPatternObservation]::GetWindowThreadProcessId($dialog,[ref]$dialogOwner) | Out-Null
  if($dialogOwner -ne $app.Id){throw 'Transpose dialog ownership changed'}
  Assert-ActiveOwned
  if($TopmostOwnedWindow){[VibeRaPatternObservation]::SetWindowPos($dialog,[IntPtr](-1),0,0,0,0,19) | Out-Null}
  [VibeRaPatternObservation]::SetForegroundWindow($dialog) | Out-Null
  $origin=New-Object VibeRaPatternObservation+Point
  [VibeRaPatternObservation]::ClientToScreen($dialog,[ref]$origin) | Out-Null
  $dialogScale=[VibeRaPatternObservation]::GetDpiForWindow($dialog)/96.0
  $point=New-Object VibeRaPatternObservation+Point
  $point.x=$origin.x+[int](100*$dialogScale);$point.y=$origin.y+[int](112*$dialogScale)
  $hitPid=[uint32]0
  [VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::WindowFromPoint($point),[ref]$hitPid) | Out-Null
  if($hitPid -ne $app.Id){throw 'Transpose text editor obscured; no input'}
  [VibeRaPatternObservation]::SetCursorPos($point.x,$point.y) | Out-Null
  [VibeRaPatternObservation]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [VibeRaPatternObservation]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
  Control-Key 65
  Key 49
  Key 50
  Capture $dialog 'native-transpose-typed.png'
  Key 13
  Control-Key 83
  $shifted=[xml](Get-Content -LiteralPath $Project -Raw)
  $expected=[xml]$before.OuterXml
  $channel=@($expected.VIBERA.CHANNELS.CHANNEL)[-1].id
  $changed=0
  foreach($note in $expected.VIBERA.PATTERNS.PATTERN[0].NOTE){
   if($note.channel -eq $channel){$note.pitch=[string]([int]$note.pitch+12);$changed++}
  }
  if($changed -eq 0 -or $expected.OuterXml -ne $shifted.OuterXml){Capture $h 'native-transpose-not-applied.png';throw 'Native selected-Channel transpose/save did not exactly preserve other project state'}
  Click 800 408 # Inspected Piano roll Fit button at this actual144DPI layout.
  Capture $h 'native-transposed-workspace.png'
  Control-Key 90
  Control-Key 83
  $restored=[xml](Get-Content -LiteralPath $Project -Raw)
  if($restored.OuterXml -ne $before.OuterXml){throw 'Native transpose Undo/Save failed exact restoration'}
  $record.Add('PASS: native Transpose menu, selected-Channel default, typing 12, Return, Ctrl+S, Piano roll Fit and one Ctrl+Z/Save; exact pitch-only XML and exact baseline restoration.')
 }
 # Focus the selector through its native dropdown, close it, then use the actual F2 key.
 Click 430 56
 Key 27
 Key 113
 $dialog=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,'Rename Pattern')
 if($dialog -eq [IntPtr]::Zero){throw 'Native F2 did not open Pattern properties'}
 Capture $dialog 'native-pattern-properties.png'
 Key 27
 if($RackWheel){
  $channels=@($before.VIBERA.CHANNELS.CHANNEL)
  if($channels.Count -lt 16){throw 'Rack wheel fixture needs at least sixteen built-in Channels'}
  function Wheel-Rack([bool]$down) {
   Assert-ActiveOwned
   $origin=New-Object VibeRaPatternObservation+Point
   [VibeRaPatternObservation]::ClientToScreen($h,[ref]$origin) | Out-Null
   $point=New-Object VibeRaPatternObservation+Point
   $point.x=$origin.x+[int](1000*$scale);$point.y=$origin.y+[int](516*$scale)
   $hitPid=[uint32]0
   [VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::WindowFromPoint($point),[ref]$hitPid) | Out-Null
   if($hitPid -ne $app.Id){throw 'Rack wheel target obscured; no input'}
   [VibeRaPatternObservation]::SetCursorPos($point.x,$point.y) | Out-Null
   $data=if($down){[uint32]4294967176}else{[uint32]120}
   [VibeRaPatternObservation]::mouse_event(2048,0,0,$data,[UIntPtr]::Zero)
   Start-Sleep -Milliseconds 90
  }
  foreach($pass in @(@{down=$true;index=8;image='native-rack-wheel-down.png'},@{down=$false;index=0;image='native-rack-wheel-up.png'})){
   for($i=0;$i -lt 8;$i++){Wheel-Rack $pass.down}
   Capture $h $pass.image
   Click 905 454
   $expectedTitle=$channels[$pass.index].name+' | Channel instrument'
   $instrument=[VibeRaPatternObservation]::FindOtherOwned([uint32]$app.Id,$h,$expectedTitle)
   if($instrument -eq [IntPtr]::Zero){throw "Rack wheel/name target did not reach $expectedTitle"}
   Capture $instrument ('native-rack-instrument-'+$pass.index+'.png')
   $owner=[uint32]0
   [VibeRaPatternObservation]::GetWindowThreadProcessId($instrument,[ref]$owner) | Out-Null
   if($owner -ne $app.Id){throw 'Instrument close ownership changed'}
   [VibeRaPatternObservation]::SendMessage($instrument,16,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
   Start-Sleep -Milliseconds 150
   [VibeRaPatternObservation]::SetForegroundWindow($h) | Out-Null
  }
  Click 109 16
  $afterWheel=[xml](Get-Content -LiteralPath $Project -Raw)
  if($afterWheel.OuterXml -ne $before.OuterXml){throw 'Native wheel/open/save changed musical project data'}
  $record.Add('PASS: eight plain OS wheel ticks down reach Channel9; eight up restore Channel1, verified by actual name-button instrument titles; exact saved project retained.')
 }
 $record.Add("PASS: actual OS mouse Pattern-menu opening, selector focus, F2 properties and Escape cancellation. PID $($app.Id), creation $creation, SHA256 $((Get-FileHash -LiteralPath $exe).Hash). No physical input, audio-device test or listening claim.")
} catch {$record.Add("NOT COMPLETED: $($_.Exception.Message)");throw}
finally {
 $id=[uint32]0;[VibeRaPatternObservation]::GetWindowThreadProcessId([VibeRaPatternObservation]::GetForegroundWindow(),[ref]$id) | Out-Null
 if($id -eq $app.Id){[VibeRaPatternObservation]::SetCursorPos($cursor.x,$cursor.y) | Out-Null}
 $forcedClose=$false
 if(($ChannelManagement -or $StepVelocity -or $Swing -or $Quantizer -or $SamplerVoices -or $LoopBounds) -and -not $app.HasExited){$app.StandardInput.Close()}
 if(-not $app.HasExited){$null=$app.CloseMainWindow();if(-not $app.WaitForExit(3000)){$forcedClose=$true;$app.Kill();$app.WaitForExit()}}
 $app.Refresh()
 $record.Add("Owned test terminal: $($app.HasExited); exitCode=$($app.ExitCode); forcedClose=$forcedClose")
 $record | Set-Content -LiteralPath (Join-Path $Output $(if($LoopBounds){'native-loop-bounds-verification.txt'}elseif($SamplerVoices){'native-sampler-voices-verification.txt'}elseif($Quantizer){'native-quantizer-verification.txt'}elseif($Swing){'native-swing-verification.txt'}elseif($StepVelocity){'native-step-velocity-verification.txt'}elseif($StepFill){'native-step-fill-verification.txt'}elseif($ChannelSolo){'native-channel-solo-verification.txt'}elseif($ChannelManagement){'native-channel-management-verification.txt'}else{'native-pattern-verification.txt'})) -Encoding UTF8
 if($forcedClose -or $app.ExitCode -ne 0){throw 'Owned native test did not close gracefully with exit0; evidence preserved'}
}
$record
