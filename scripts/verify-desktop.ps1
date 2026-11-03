param(
    [string]$Project = (Join-Path $PSScriptRoot '../dist/VibeRa Studio/Examples/Midnight Session.vibra'),
    [string]$Output = (Join-Path $PSScriptRoot '../build/windows/desktop_acceptance')
)
# Opt-in: opens a visible instance, sends synthetic Windows input, captures only its client area.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'engineering-owner.ps1')
$taskOwnerExit=Invoke-VibeRaEngineeringOwner -Purpose 'native desktop' -Script $PSCommandPath -Parameters $PSBoundParameters
if($null -ne $taskOwnerExit){exit $taskOwnerExit}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class VibeRaDesktop {
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
 [StructLayout(LayoutKind.Sequential)] public struct Point { public int x,y; }
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd,out Rect rect);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd,out Rect rect);
 [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hwnd,ref Point point);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd,int command);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd,IntPtr after,int x,int y,int w,int h,uint flags);
 [DllImport("user32.dll")] public static extern void keybd_event(byte key,byte scan,uint flags,UIntPtr extra);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
 [DllImport("user32.dll")] public static extern void mouse_event(uint flags,uint dx,uint dy,uint data,UIntPtr extra);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd,uint message,IntPtr w,IntPtr l);
}
'@
[VibeRaDesktop]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
$exe = Join-Path $root 'dist/VibeRa Studio/VibeRa Studio.exe'
$projectPath = (Resolve-Path -LiteralPath $Project).Path
# The user requested observation/interaction of the running application.
$app = Start-Process -FilePath $exe -ArgumentList @('--open-project',('"'+$projectPath+'"')) -PassThru -WindowStyle Normal
$handle = [IntPtr]::Zero
for ($i=0;$i -lt 40;$i++) {
    Start-Sleep -Milliseconds 250
    $app.Refresh()
    if ($app.HasExited) { throw 'Review instance exited before showing a window.' }
    $handle=$app.MainWindowHandle
    if ($handle -ne [IntPtr]::Zero) { break }
}
if ($handle -eq [IntPtr]::Zero) { throw 'No review window.' }
$app.WaitForInputIdle(5000) | Out-Null
Start-Sleep -Milliseconds 500
$app.Refresh()
$handle=$app.MainWindowHandle
$record = [System.Collections.Generic.List[string]]::new()
$record.Add("Process: $($app.Id); project: $projectPath")
$record.Add("Executable SHA256: $((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash)")
$record.Add("Physical primary: $([VibeRaDesktop]::GetSystemMetrics(0)) x $([VibeRaDesktop]::GetSystemMetrics(1))")
function Assert-Foreground {
    [VibeRaDesktop]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Milliseconds 150
    if ([VibeRaDesktop]::GetForegroundWindow() -ne $handle) {
        $foregroundPid=[uint32]0
        [VibeRaDesktop]::GetWindowThreadProcessId([VibeRaDesktop]::GetForegroundWindow(),[ref]$foregroundPid) | Out-Null
        throw "VibeRa did not acquire foreground (expected HWND $handle / PID $($app.Id), foreground PID $foregroundPid); no input or screenshot sent."
    }
}
function Capture-App([string]$Name) {
    Assert-Foreground
    Start-Sleep -Milliseconds 300
    $r=New-Object VibeRaDesktop+Rect
    $p=New-Object VibeRaDesktop+Point
    [VibeRaDesktop]::GetClientRect($handle,[ref]$r) | Out-Null
    [VibeRaDesktop]::ClientToScreen($handle,[ref]$p) | Out-Null
    $bitmap=New-Object System.Drawing.Bitmap(($r.right-$r.left),($r.bottom-$r.top))
    $graphics=[System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($p.x,$p.y,0,0,$bitmap.Size)
        $bitmap.Save((Join-Path $Output $Name),[System.Drawing.Imaging.ImageFormat]::Png)
        $record.Add("$Name : $($bitmap.Width) x $($bitmap.Height) pixels; actual window DPI $([VibeRaDesktop]::GetDpiForWindow($handle))")
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}
function Key([byte]$Code) {
    Assert-Foreground
    [VibeRaDesktop]::keybd_event($Code,0,0,[UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [VibeRaDesktop]::keybd_event($Code,0,2,[UIntPtr]::Zero)
    Start-Sleep -Milliseconds 200
}
function Click-Logical([double]$X,[double]$Y,[bool]$Caption=$false) {
    Assert-Foreground
    $p=New-Object VibeRaDesktop+Point
    [VibeRaDesktop]::ClientToScreen($handle,[ref]$p) | Out-Null
    $scale=[VibeRaDesktop]::GetDpiForWindow($handle)/96.0
    if (-not $Caption) { $Y += 36 }
    [VibeRaDesktop]::SetCursorPos(($p.x+[int]($X*$scale)),($p.y+[int]($Y*$scale))) | Out-Null
    [VibeRaDesktop]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [VibeRaDesktop]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
    Start-Sleep -Milliseconds 200
}
$cursor=New-Object VibeRaDesktop+Point
[VibeRaDesktop]::GetCursorPos([ref]$cursor) | Out-Null
try {
    [VibeRaDesktop]::ShowWindow($handle,3) | Out-Null
    Capture-App 'desktop-4k-populated.png'
    Key 117 # F6 hide Channel Rack
    Capture-App 'desktop-4k-rack-hidden.png'
    Key 117
    Key 120 # F9 hide Mixer
    Capture-App 'desktop-4k-mixer-hidden.png'
    Key 120
    $r=New-Object VibeRaDesktop+Rect
    [VibeRaDesktop]::GetClientRect($handle,[ref]$r) | Out-Null
    $scale=[VibeRaDesktop]::GetDpiForWindow($handle)/96.0
    $logicalWidth=$r.right/$scale
    $logicalHeight=$r.bottom/$scale-36
    Click-Logical ($logicalWidth-508) (84+[Math]::Floor(($logicalHeight-113-194-7-7)*.52)+7+15)
    Capture-App 'desktop-4k-piano-maximised.png'
    Click-Logical ($logicalWidth-51) 99
    Capture-App 'desktop-4k-restored.png'
    Assert-Foreground
    $p=New-Object VibeRaDesktop+Point
    [VibeRaDesktop]::ClientToScreen($handle,[ref]$p) | Out-Null
    $scale=[VibeRaDesktop]::GetDpiForWindow($handle)/96.0
    [VibeRaDesktop]::SetCursorPos(($p.x+[int](235*$scale)),($p.y+[int](240*$scale))) | Out-Null
    [VibeRaDesktop]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
    for($j=1;$j -le 12;$j++) {
        [VibeRaDesktop]::SetCursorPos(($p.x+[int]((235+$j*5)*$scale)),($p.y+[int](240*$scale))) | Out-Null
        Start-Sleep -Milliseconds 20
    }
    [VibeRaDesktop]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
    Capture-App 'desktop-4k-browser-resized.png'
    [VibeRaDesktop]::ShowWindow($handle,1) | Out-Null
    [VibeRaDesktop]::SetWindowPos($handle,[IntPtr]::Zero,120,100,1920,1080,4) | Out-Null
    Capture-App 'desktop-1080-viewport.png'
    # Use native Windows hit testing, then actual bounded caption input.
    $native=New-Object VibeRaDesktop+Rect
    [VibeRaDesktop]::GetClientRect($handle,[ref]$native) | Out-Null
    $point=New-Object VibeRaDesktop+Point
    [VibeRaDesktop]::ClientToScreen($handle,[ref]$point) | Out-Null
    $scale=[VibeRaDesktop]::GetDpiForWindow($handle)/96.0
    foreach ($probe in @(@('caption',300,18,2),@('minimize',($native.right/$scale-115),18,8),@('maximize',($native.right/$scale-69),18,9),@('close',($native.right/$scale-23),18,20))) {
        $screenX=$point.x+[int]($probe[1]*$scale)
        $screenY=$point.y+[int]($probe[2]*$scale)
        $packed=($screenY -shl 16) -bor ($screenX -band 65535)
        $hit=[VibeRaDesktop]::SendMessage($handle,132,[IntPtr]::Zero,[IntPtr]$packed).ToInt64()
        $record.Add("Native hit test $($probe[0]): $hit (expected $($probe[3]))")
        if ($hit -ne $probe[3]) { throw "Incorrect native hit test: $($probe[0])" }
    }
    Click-Logical ($native.right/$scale-69) 18 $true
    if (-not [VibeRaDesktop]::IsZoomed($handle)) { throw 'Custom maximize button failed.' }
    Capture-App 'desktop-dark-title-maximised.png'
    [VibeRaDesktop]::GetClientRect($handle,[ref]$native) | Out-Null
    Click-Logical ($native.right/$scale-69) 18 $true
    if ([VibeRaDesktop]::IsZoomed($handle)) { throw 'Custom restore button failed.' }
    $record.Add('Custom caption native hit tests and OS mouse maximize/restore passed.')
    $baseline=(Get-FileHash -LiteralPath (Join-Path $Output 'desktop-4k-populated.png')).Hash
    foreach($name in @('desktop-4k-rack-hidden.png','desktop-4k-mixer-hidden.png','desktop-4k-piano-maximised.png','desktop-4k-browser-resized.png')) {
        if((Get-FileHash -LiteralPath (Join-Path $Output $name)).Hash -eq $baseline) { throw "No visible change: $name" }
    }
    $record.Add('Performed: running Windows window display, F6/F9 via OS synthetic keyboard input, Piano Roll maximise/restore via OS mouse clicks, Browser divider via OS synthetic mouse drag, native window resizing. Nonidentical captures checked; outcomes also require screenshot inspection.')
    $record.Add('PASS: bounded native desktop capture and synthetic input verification completed.')
    $record.Add('Not performed: human physical input, listening, audio/MIDI/latency hardware qualification, Windows display-scale changes, physical 1080p monitor test, cross-monitor DPI transitions.')
} catch {
    $record.Add("NOT COMPLETED: $($_.Exception.Message)")
    throw
} finally {
    [VibeRaDesktop]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
    [VibeRaDesktop]::SetCursorPos($cursor.x,$cursor.y) | Out-Null
    $record | Set-Content -LiteralPath (Join-Path $Output 'desktop-verification.txt') -Encoding UTF8
    # Gracefully close only this test-owned instance. Never terminate other sessions.
    if ([VibeRaDesktop]::GetForegroundWindow() -eq $handle) {
        $r=New-Object VibeRaDesktop+Rect
        [VibeRaDesktop]::GetClientRect($handle,[ref]$r) | Out-Null
        Click-Logical ($r.right/([VibeRaDesktop]::GetDpiForWindow($handle)/96.0)-23) 18 $true
    } else { [VibeRaDesktop]::PostMessage($handle,16,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null }
}
$record
