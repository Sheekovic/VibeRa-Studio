param([string]$Output=(Join-Path $PSScriptRoot '../build/gesture-os-input'), [switch]$UseGlobalInput)
$ErrorActionPreference='Stop'
if(-not $UseGlobalInput){throw 'Scoped native input requires -UseGlobalInput.'}
$root=Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force -Path $Output | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System; using System.Runtime.InteropServices; using System.Drawing; using System.Drawing.Imaging;
public static class VibeRaSamplerNative {
 public delegate bool EnumCallback(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)] public struct Point { public int x,y; }
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumCallback callback,IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,System.Text.StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint id);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point p);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref Point p);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr h,uint flags);
 [StructLayout(LayoutKind.Sequential)] public struct MonitorInfo { public uint size; public Rect monitor,work; public uint flags; }
 [DllImport("user32.dll")] public static extern bool GetMonitorInfo(IntPtr h,ref MonitorInfo info);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
 [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point p);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
 [StructLayout(LayoutKind.Sequential)] public struct LastInputInfo { public uint size,time; }
 [DllImport("user32.dll")] public static extern bool GetLastInputInfo(ref LastInputInfo info);
 [DllImport("kernel32.dll")] public static extern uint GetTickCount();
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint data,UIntPtr extra);
 [DllImport("user32.dll")] public static extern void keybd_event(byte key,byte scan,uint flags,UIntPtr extra);
 [DllImport("user32.dll")] public static extern IntPtr GetWindowDC(IntPtr h);
 [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h,IntPtr dc);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr dc);
 [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleBitmap(IntPtr dc,int w,int h);
 [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr dc,IntPtr o);
 [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr o);
 [DllImport("gdi32.dll")] public static extern bool DeleteDC(IntPtr dc);
 public static IntPtr Find(uint pid,string title) {
  IntPtr found=IntPtr.Zero; EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);
   var text=new System.Text.StringBuilder(256);GetWindowText(h,text,256);
   if(id==pid && IsWindowVisible(h) && text.ToString()==title){found=h;return false;} return true;
  },IntPtr.Zero); return found;
 }
 public static void Capture(IntPtr h,uint expectedPid,string path) {
  uint actualPid;GetWindowThreadProcessId(h,out actualPid);
  if(actualPid!=expectedPid)throw new Exception("Sampler capture ownership lost");
  Rect r;GetWindowRect(h,out r);int w=r.right-r.left,ht=r.bottom-r.top;
  var info=new MonitorInfo();info.size=(uint)Marshal.SizeOf(typeof(MonitorInfo));
  if(!GetMonitorInfo(MonitorFromWindow(h,2),ref info))throw new Exception("Owned monitor geometry missing");
  if(w>info.work.right-info.work.left || ht>info.work.bottom-info.work.top)throw new Exception("Owned sampler does not fit work area; no screen capture");
  int x=Math.Max(info.work.left,Math.Min(r.left,info.work.right-w));
  int y=Math.Max(info.work.top,Math.Min(r.top,info.work.bottom-ht));
  if(x!=r.left || y!=r.top){SetWindowPos(h,IntPtr.Zero,x,y,0,0,21);System.Threading.Thread.Sleep(150);GetWindowRect(h,out r);}
  uint pid;GetWindowThreadProcessId(h,out pid);
  foreach(var point in new[]{new Point{x=r.left+20,y=r.top+20},new Point{x=r.right-20,y=r.top+20},new Point{x=r.left+20,y=r.bottom-20},new Point{x=r.right-20,y=r.bottom-20}}) {
   uint owner;GetWindowThreadProcessId(WindowFromPoint(point),out owner);
   if(owner!=pid)throw new Exception("Owned sampler capture is obscured; no unrelated pixels captured");
  }
  using(var image=new Bitmap(w,ht,PixelFormat.Format32bppArgb)) {
   using(var graphics=Graphics.FromImage(image))graphics.CopyFromScreen(r.left,r.top,0,0,new Size(w,ht));
   image.Save(path,ImageFormat.Png);
  }
 }

}
'@
[VibeRaSamplerNative]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
$exe=Join-Path $root 'dist/VibeRa Studio/VibeRa Studio.exe'
$info=New-Object System.Diagnostics.ProcessStartInfo
$info.FileName=$exe; $info.Arguments='--mcp-test'; $info.UseShellExecute=$false
$info.RedirectStandardInput=$true; $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
$info.WindowStyle=[System.Diagnostics.ProcessWindowStyle]::Hidden
$app=New-Object System.Diagnostics.Process
$app.StartInfo=$info
if(-not $app.Start()){throw 'Owned MCP test app did not start'}
$ownedHandle=$app.Handle; $creation=$app.StartTime
$errorRead=$app.StandardError.ReadToEndAsync()
$script:serial=0; $script:heldButton=0; $script:cursorOwned=$false
$originalCursor=New-Object VibeRaSamplerNative+Point
[VibeRaSamplerNative]::GetCursorPos([ref]$originalCursor) | Out-Null
$script:lastCursor=$originalCursor
$report=[System.Collections.Generic.List[string]]::new()
function Request([string]$method,$params) {
 $script:serial++
 $message=@{jsonrpc='2.0';id=$script:serial;method=$method;params=$params}
 $app.StandardInput.WriteLine(($message | ConvertTo-Json -Depth 60 -Compress)); $app.StandardInput.Flush()
 $read=$app.StandardOutput.ReadLineAsync()
 if(-not $read.Wait(15000)){throw 'Owned MCP response timeout'}
 $reply=$read.Result | ConvertFrom-Json
 if($null -ne $reply.error){throw ($reply.error | ConvertTo-Json -Compress)}
 return $reply.result
}
function Call([string]$name,$arguments) {
 $result=Request 'tools/call' @{name=$name;arguments=$arguments}
 if($result.isError){throw ($result | ConvertTo-Json -Depth 8 -Compress)}
 return $result.structuredContent
}
function Check-Owned {
 if($app.HasExited -or $app.StartTime -ne $creation){throw 'Owned app identity lost'}
 $pidAtWindow=[uint32]0
 [VibeRaSamplerNative]::GetWindowThreadProcessId($script:window,[ref]$pidAtWindow) | Out-Null
 if($pidAtWindow -ne $app.Id){throw 'Sampler window identity lost'}
}
function Check-Input {
 Check-Owned
 $id=[uint32]0
 [VibeRaSamplerNative]::GetWindowThreadProcessId([VibeRaSamplerNative]::GetForegroundWindow(),[ref]$id) | Out-Null
 if($id -ne $app.Id){throw 'Foreground ownership lost; native input paused'}
 foreach($key in @(1,2,4,16,17,18)) {
  if($key -ne $script:heldButton -and ([VibeRaSamplerNative]::GetAsyncKeyState($key) -band 32768)){throw 'User input is active; native input paused'}
 }
 $current=New-Object VibeRaSamplerNative+Point
 [VibeRaSamplerNative]::GetCursorPos([ref]$current) | Out-Null
 if($script:cursorOwned -and ($current.x -ne $script:lastCursor.x -or $current.y -ne $script:lastCursor.y)){throw 'Cursor changed outside this test; native input paused'}
}
function Set-OwnedSamplerPointer([double]$x,[double]$y) {
 Check-Input
 $point=New-Object VibeRaSamplerNative+Point
 [VibeRaSamplerNative]::ClientToScreen($script:window,[ref]$point) | Out-Null
 $point.x += [int]($x*$script:scale); $point.y += [int]($y*$script:scale)
 $id=[uint32]0
 [VibeRaSamplerNative]::GetWindowThreadProcessId([VibeRaSamplerNative]::WindowFromPoint($point),[ref]$id) | Out-Null
 if($id -ne $app.Id){throw 'Owned sampler target obscured; native input paused'}
 [VibeRaSamplerNative]::SetCursorPos($point.x,$point.y) | Out-Null
 $script:lastCursor=$point; $script:cursorOwned=$true
}
function Click([double]$x,[double]$y,[bool]$right=$false) {
 Set-OwnedSamplerPointer $x $y
 $script:heldButton=if($right){2}else{1}
 [VibeRaSamplerNative]::mouse_event($(if($right){8}else{2}),0,0,0,[UIntPtr]::Zero)
 [VibeRaSamplerNative]::mouse_event($(if($right){16}else{4}),0,0,0,[UIntPtr]::Zero)
 $script:heldButton=0
 Start-Sleep -Milliseconds 120
}
try {
 Request 'initialize' @{protocolVersion='2025-11-25';capabilities=@{};clientInfo=@{name='Owned native sampler test';version='1'}} | Out-Null
 $app.StandardInput.WriteLine('{"jsonrpc":"2.0","method":"notifications/initialized"}');$app.StandardInput.Flush()
 Call 'vibera_editor' @{editor=0;action='show'} | Out-Null
 Call 'vibera_pointer' @{editor=0;phase='down';x=55;y=60;button='left'} | Out-Null
 Call 'vibera_pointer' @{editor=0;phase='up';x=55;y=60;button='left'} | Out-Null
 Start-Sleep -Milliseconds 300
 $state=Call 'vibera_state' @{}
 $view=@($state.samplerWindows | Where-Object {$_.visible})[0]
 if($null -eq $view){throw 'Sampler window state missing'}
 $channel=@($state.channels | Where-Object {$_.id -eq $view.channel})[0]
 $script:window=[VibeRaSamplerNative]::Find([uint32]$app.Id,($channel.name+' | Channel instrument'))
 if($script:window -eq [IntPtr]::Zero){throw 'Owned visible sampler window missing'}
 Check-Owned
 $dpi=[VibeRaSamplerNative]::GetDpiForWindow($script:window); $script:scale=$dpi/96.0
 [VibeRaSamplerNative]::SetForegroundWindow($script:window) | Out-Null
 Start-Sleep -Milliseconds 150
 $foreground=[uint32]0
 [VibeRaSamplerNative]::GetWindowThreadProcessId([VibeRaSamplerNative]::GetForegroundWindow(),[ref]$foreground) | Out-Null
 if($foreground -ne $app.Id) {
  foreach($key in 1..255){if(([VibeRaSamplerNative]::GetAsyncKeyState($key) -band 32768) -ne 0){throw 'User input active before activation; no caption input'}}
  $last=New-Object VibeRaSamplerNative+LastInputInfo;$last.size=[System.Runtime.InteropServices.Marshal]::SizeOf($last)
  [VibeRaSamplerNative]::GetLastInputInfo([ref]$last) | Out-Null
  if(([int64][VibeRaSamplerNative]::GetTickCount()-[int64]$last.time) -lt 500){throw 'Recent user input; activation paused'}
  $point=New-Object VibeRaSamplerNative+Point;[VibeRaSamplerNative]::ClientToScreen($script:window,[ref]$point) | Out-Null
  $point.x += [int](200*$script:scale);$point.y += [int](18*$script:scale)
  if([VibeRaSamplerNative]::WindowFromPoint($point) -ne $script:window){throw 'Owned caption obscured; no activation input'}
  [VibeRaSamplerNative]::SetCursorPos($point.x,$point.y) | Out-Null;$script:lastCursor=$point;$script:cursorOwned=$true
  [VibeRaSamplerNative]::mouse_event(2,0,0,0,[UIntPtr]::Zero);[VibeRaSamplerNative]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
  Start-Sleep -Milliseconds 120
 }
 Check-Input
 $button=$view.controls.'instrument.sampler.showKeyboard'
 Click ($button.x+$button.width/2) ($button.y+$button.height/2)
 $state=Call 'vibera_state' @{}
 $view=@($state.samplerWindows | Where-Object {$_.channel -eq $channel.id})[0]
 $key=@($view.keys | Where-Object {$_.pitch -eq 72})[0].bounds
 if($null -eq $key -or $key.y+$key.height -gt $view.bounds.height){throw 'Keyboard was not scrolled into view'}
 Click ($key.x+$key.width/2) ($key.y+$key.height-8) $true
 $state=Call 'vibera_state' @{}
 $rootValue=@($state.channels | Where-Object {$_.id -eq $channel.id})[0].sampler.root
 if($rootValue -ne 72){throw 'Native right-click did not select MIDI root72'}
 [VibeRaSamplerNative]::Capture($script:window,[uint32]$app.Id,(Join-Path $Output 'native-sampler-root.png'))
 Call 'vibera_command' @{command='edit.undo'} | Out-Null
 # Native wheel scroll in this owned viewport reveals the source handles.
 Set-OwnedSamplerPointer 10 ($view.bounds.height/2)
 [VibeRaSamplerNative]::mouse_event(2048,0,0,1200,[UIntPtr]::Zero)
 Start-Sleep -Milliseconds 180
 $state=Call 'vibera_state' @{}
 $view=@($state.samplerWindows | Where-Object {$_.channel -eq $channel.id})[0]
 $range=$view.controls.'instrument.sampler.waveform'
 if($range.y -lt 36){throw 'Source waveform is still clipped; native drag paused'}
 $beforeView=$view.waveformViewLength
 $beforeRevision=$state.revision
 Set-OwnedSamplerPointer ($range.x+$range.width/2) ($range.y+$range.height/2)
 [VibeRaSamplerNative]::mouse_event(2048,0,0,120,[UIntPtr]::Zero)
 Start-Sleep -Milliseconds 150
 $zoomState=Call 'vibera_state' @{}
 $zoomView=@($zoomState.samplerWindows | Where-Object {$_.channel -eq $channel.id})[0]
 if($zoomView.waveformViewLength -ge $beforeView -or $zoomState.revision -ne $beforeRevision){throw 'Native waveform wheel zoom failed or changed document'}
 [VibeRaSamplerNative]::Capture($script:window,[uint32]$app.Id,(Join-Path $Output 'native-sampler-wheel-zoom.png'))
 $oldBegin=$zoomView.waveformViewStart
 Set-OwnedSamplerPointer ($range.x+$range.width/2) ($range.y+$range.height-9)
 $script:heldButton=1
 [VibeRaSamplerNative]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
 for($i=1;$i -le 5;$i++){Set-OwnedSamplerPointer ($range.x+$range.width/2+$i*6) ($range.y+$range.height-9);Start-Sleep -Milliseconds 25}
 [VibeRaSamplerNative]::mouse_event(4,0,0,0,[UIntPtr]::Zero);$script:heldButton=0
 Start-Sleep -Milliseconds 150
 $panState=Call 'vibera_state' @{}
 $panView=@($panState.samplerWindows | Where-Object {$_.channel -eq $channel.id})[0]
 if($panView.waveformViewStart -le $oldBegin -or $panState.revision -ne $beforeRevision){throw 'Native waveform scrollbar did not pan or changed document'}
 Set-OwnedSamplerPointer ($range.x+$range.width/2) ($range.y+$range.height/2)

 [VibeRaSamplerNative]::mouse_event(2048,0,0,[uint32]4294967176,[UIntPtr]::Zero)
 Start-Sleep -Milliseconds 150
 $state=Call 'vibera_state' @{}
 $view=@($state.samplerWindows | Where-Object {$_.channel -eq $channel.id})[0]
 if([Math]::Abs($view.waveformViewLength-1) -gt .0001){throw 'Native waveform zoom-out did not restore full source'}

 $start=$range.x+8; $end=$range.x+8+($range.width-16)*.25; $y=$range.y+8
 Set-OwnedSamplerPointer $start $y
 $script:heldButton=1
 [VibeRaSamplerNative]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
 for($i=1;$i -le 8;$i++){Set-OwnedSamplerPointer ($start+($end-$start)*$i/8) $y;Start-Sleep -Milliseconds 25}
 [VibeRaSamplerNative]::mouse_event(4,0,0,0,[UIntPtr]::Zero);$script:heldButton=0
 Start-Sleep -Milliseconds 150
 $state=Call 'vibera_state' @{}
 $startValue=@($state.channels | Where-Object {$_.id -eq $channel.id})[0].sampler.start
 if([Math]::Abs($startValue-.25) -gt .01){throw 'Native source-handle drag did not change sampler range'}
 [VibeRaSamplerNative]::Capture($script:window,[uint32]$app.Id,(Join-Path $Output 'native-sampler-range.png'))
 Call 'vibera_command' @{command='edit.undo'} | Out-Null
 $state=Call 'vibera_state' @{}
 $restored=@($state.channels | Where-Object {$_.id -eq $channel.id})[0].sampler
 if($restored.root -ne -1 -or $restored.start -ne 0 -or $restored.end -ne 1){throw 'Native sampler gestures did not undo independently'}
 $report.Add('PASS: owned OS right-click root72, Keyboard button/viewport scroll, native waveform wheel zoom/out and scrollbar drag with unchanged revision, native mouse source-start drag, MCP readback and separate Undo. Preparatory Channel opening used paired component handlers.')
 $report.Add("Owned PID $($app.Id), creation $($creation.ToUniversalTime().ToString('o')); DPI$dpi; executable SHA256 $((Get-FileHash -LiteralPath $exe).Hash)")
 $report.Add('Only the unobscured owned sampler rectangle was captured from the native desktop. No physical input, audio playback, listening or hardware qualification; virtual audio checked separately.')
} catch {
 $report.Add('NOT COMPLETED: '+$_.Exception.Message)
 throw
} finally {
 if($script:heldButton){[VibeRaSamplerNative]::mouse_event($(if($script:heldButton -eq 2){16}else{4}),0,0,0,[UIntPtr]::Zero);$script:heldButton=0}
 if($script:cursorOwned -and -not $app.HasExited) {
  $id=[uint32]0;[VibeRaSamplerNative]::GetWindowThreadProcessId([VibeRaSamplerNative]::GetForegroundWindow(),[ref]$id) | Out-Null
  $current=New-Object VibeRaSamplerNative+Point;[VibeRaSamplerNative]::GetCursorPos([ref]$current) | Out-Null
  if($id -eq $app.Id -and $current.x -eq $script:lastCursor.x -and $current.y -eq $script:lastCursor.y){[VibeRaSamplerNative]::SetCursorPos($originalCursor.x,$originalCursor.y) | Out-Null}
 }
 if(-not $app.HasExited){$app.StandardInput.Close();if(-not $app.WaitForExit(10000)){
  if($app.StartTime -eq $creation){$app.CloseMainWindow() | Out-Null;if(-not $app.WaitForExit(2000)){$app.Kill();$app.WaitForExit(5000) | Out-Null}}
 }}
 $report.Add("Owned test terminal: $($app.HasExited); exit $($app.ExitCode)")
 $report | Set-Content -LiteralPath (Join-Path $Output ('native-sampler-attempt-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssfff')+'.txt')) -Encoding UTF8
 if(@($report | Where-Object {$_ -like 'PASS:*'}).Count){$report | Set-Content -LiteralPath (Join-Path $Output 'native-sampler-verification.txt') -Encoding UTF8}
 else {$report | Set-Content -LiteralPath (Join-Path $Output 'native-sampler-pending.txt') -Encoding UTF8}
 if($errorRead.IsCompleted){$errorRead.Result | Set-Content -LiteralPath (Join-Path $Output 'stderr.log') -Encoding UTF8}
}
