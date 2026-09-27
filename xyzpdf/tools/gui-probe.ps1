<#
    gui-probe.ps1 -- the UI Automation routes this project has actually measured, in one
    tracked file. Before this, every real-window pass was written from scratch in a temp
    directory against a script that no longer exists, and the routes had to be re-derived
    round by round (three rounds were lost on 2026-09-27 to "the export line never
    appears", which was only ever a wrong property read -- see Get-StatusName).

    It drives the *dev* build by default and needs no keyboard, no clipboard and no
    foreground window: menu rows come from ExpandCollapse, our own Qt fields are read as
    UIA properties, and the one native dialog in the way is driven with Win32 messages.

    Modes:
      inventory      every non-empty Name in the window, with its control type. This is
                     the dump that caught two product defects the gate could not see.
      menudump       expand one top-level menu and print its rows (-Menu Document).
      exportstatus   run the image export through the real picker and read the status line back
                     two ways at once (-OutDir must exist; it writes files there).
      mergedonor     merge a one-page then a two-page donor through the native open dialog and
                     read the undo row after each, which is the only row whose text names a file
                     (-LastDir must hold -Donor; nothing is saved, the run ends by killing itself).

    Examples (from the repository root):
      powershell -NoProfile -ExecutionPolicy Bypass -File xyzpdf/tools/gui-probe.ps1 -Mode inventory
      ... -Mode menudump -Menu Document
      ... -Mode exportstatus -OutDir H:/tmp/xyzpdf-probe
      ... -Mode mergedonor -LastDir H:/tmp/xyzpdf-probe -Donor donor1.pdf

    -Log takes a UTF-8 file, which is the only way to read a Chinese interface back from this
    console (it is GBK, so the window title alone arrives as mojibake). Write such a path with
    forward slashes: a backslashed one handed in from Git Bash loses its separators and the log
    lands as ./tmpfoo.txt in the repository root.
#>
param(
  [ValidateSet('inventory', 'menudump', 'exportstatus', 'mergedonor')]
  [string]$Mode = 'inventory',
  [string]$Exe = 'H:\test\XYZPDF\xyzpdf\build-local-release\xyzpdf.exe',
  [string]$Doc = 'H:\tmp\num34\doc40.pdf',
  [string]$OutDir = 'H:\tmp\num34\export',
  # 'en' or 'zh_CN'. Written into a scratch ini rather than the registry, because the
  # language is read once at startup and the user's own setting should stay untouched.
  [string]$Language = 'en',
  [string]$Menu = 'Document',
  # Seeded into the same scratch ini so the native open dialog starts in the folder that
  # holds the donor file -- without this the picker opens wherever the last real save was
  # and the row to click is not in view.
  [string]$LastDir = '',
  [string]$Donor = 'donor1.pdf',
  [string]$Log = ''
)

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName WindowsBase
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint d, int e, int extra, IntPtr h);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr hParent, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  public static string Cls(IntPtr h) { var b = new StringBuilder(64); GetClassNameW(h, b, 64); return b.ToString(); }
  public static string Text(IntPtr h) { var b = new StringBuilder(300); GetWindowTextW(h, b, 300); return b.ToString(); }
  public static void Click(int x, int y) {
    SetCursorPos(x, y);
    mouse_event(0x0002, 0, 0, 0, IntPtr.Zero);
    System.Threading.Thread.Sleep(60);
    mouse_event(0x0004, 0, 0, 0, IntPtr.Zero);
  }
}
"@

$AE = [System.Windows.Automation.AutomationElement]
$TS = [System.Windows.Automation.TreeScope]
$PC = [System.Windows.Automation.PropertyCondition]
$ANY = [System.Windows.Automation.Condition]::TrueCondition
$CT = [System.Windows.Automation.ControlType]
$lines = New-Object System.Collections.Generic.List[string]
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) 'xyzpdf-gui-probe'

function Say([string]$s) {
  $script:lines.Add($s)
  # Straight to the console rather than Write-Output: a function's return value is
  # everything it writes to the pipeline, so a diagnostic printed inside a helper would
  # become part of what that helper returns.
  [Console]::Out.WriteLine($s)
  if ($Log) { $script:lines | Out-File -Encoding UTF8 $Log }
}
function CjStr([int[]]$codePoints) {
  # This file must stay pure ASCII: Windows PowerShell reads a BOM-less .ps1 as ANSI, so a
  # literal Chinese character is corrupt before the comparison runs. Match strings for the
  # Chinese interface are built from code points instead.
  -join ($codePoints | ForEach-Object { [char]$_ })
}
function Pat($e, [string]$name) {
  # GetCurrentPattern THROWS for an unsupported pattern (it does not return null), so the
  # supported list is consulted first.
  $p = ($e.GetSupportedPatterns() | Where-Object { $_.ProgrammaticName -like ('*' + $name + '*') })
  if (-not $p) { return $null }
  return $e.GetCurrentPattern($p)
}
function FindType($root, $what) {
  return @($root.FindAll($TS::Descendants, (New-Object $PC($AE::ControlTypeProperty, $what))))
}
function ClickAt($e) {
  # A real click at the element's own rectangle. InvokePattern is NOT a substitute on a
  # menu row that runs exec(): it takes the whole UIA connection down.
  $r = $e.Current.BoundingRectangle
  [W32]::Click([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2))
}
function Window($procId) {
  # A killed instance can hand the document to the dying one (single-instance forwarding),
  # which leaves a window titled just "XYZPDF" and no document -- so callers poll for a
  # title that names the file rather than taking the first window they see.
  foreach ($w in @($AE::RootElement.FindAll($TS::Children, (New-Object $PC($AE::ProcessIdProperty, $procId))))) {
    if ($w.Current.Name -like '*.pdf*') { return $w }
  }
  return $null
}
function Dialog($procId, [string]$titleNeedle) {
  # Our dialogs are window-modal children of the main window: they live *inside* its
  # subtree, so scanning the desktop's top-level windows misses a dialog that is plainly
  # on screen.
  foreach ($e in @(FindType (Window $procId) $CT::Window)) {
    if ($e.Current.Name -like ('*' + $titleNeedle + '*')) { return $e }
  }
  return $null
}
function Elem($root, $type, [string]$needle) {
  foreach ($e in @(FindType $root $type)) { if ($e.Current.Name -like ('*' + $needle + '*')) { return $e } }
  return $null
}
function TopMenu($procId, [string]$which) {
  foreach ($bar in @(FindType (Window $procId) $CT::MenuBar)) {
    foreach ($t in @($bar.FindAll($TS::Children, $ANY))) {
      if ($t.Current.Name -like ('*' + $which + '*')) {
        $xp = Pat $t 'ExpandCollapse'
        if ($xp) { $xp.Expand() } else { ClickAt $t }
        Start-Sleep -Milliseconds 800
        return $t
      }
    }
  }
  Say ('  no top-level menu matching [' + $which + ']')
  return $null
}
function MenuRow($procId, [string]$needle) {
  foreach ($e in @($AE::RootElement.FindAll($TS::Descendants, (New-Object $PC($AE::ProcessIdProperty, $procId))))) {
    if ($e.Current.ControlType.ProgrammaticName -eq 'ControlType.MenuItem' `
        -and $e.Current.Name -like ('*' + $needle + '*')) { return $e }
  }
  return $null
}
function StatusName($procId) {
  # A status-bar message is the StatusBar element's OWN Name, not a child Text element:
  # QStatusBar paints showMessage() text itself, and only the permanent widgets (page
  # number, zoom) are child QLabels. Filtering on ControlType.Text therefore reports
  # "the message never appeared" for a message that is plainly on screen.
  # Caveat measured the same day: this Name can lag what is drawn (the .NET UIA client
  # caches element properties), so a *liveness* question needs Get-StatusInk as well.
  foreach ($sb in @(FindType (Window $procId) $CT::StatusBar)) { return $sb.Current.Name }
  return '(no StatusBar element)'
}
function StatusInk($procId, [string]$png) {
  # Non-white sample points in the message half of the status bar: everything left of the
  # permanent labels. Returns -1 when the geometry could not be read.
  # The strip is not blank at rest (page content bleeds into the bottom of the crop), so
  # the number to read is the DIFFERENCE between two samples, never "non-zero means text".
  $win = Window $procId
  if (-not $win) { return -1 }
  $sb = $null
  foreach ($c in @(FindType $win $CT::StatusBar)) { $sb = $c; break }
  if (-not $sb) { return -1 }
  $r = $sb.Current.BoundingRectangle
  # DIP -> device pixels must come from the window's own DPI. Deriving a scale from
  # GetWindowRect is wrong: that rectangle includes the invisible border.
  $scale = [W32]::GetDpiForWindow([IntPtr]$win.Current.NativeWindowHandle) / 96.0
  $left = [int][math]::Round($r.X * $scale)
  $top = [int][math]::Round($r.Y * $scale)
  $height = [int][math]::Round($r.Height * $scale) + 4
  $right = [int][math]::Round(($r.X + $r.Width) * $scale)
  foreach ($t in @(FindType $sb $CT::Text)) {
    $tr = $t.Current.BoundingRectangle
    if ($tr.X -lt ($right / $scale)) { $right = [int][math]::Round($tr.X * $scale) }
  }
  $right -= 4
  $width = $right - $left
  if ($width -lt 20 -or $height -lt 4) { return -1 }
  $bmp = New-Object System.Drawing.Bitmap($width, $height)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($left, $top, 0, 0, (New-Object System.Drawing.Size($width, $height)))
  if ($png) { $bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png) }
  $ink = 0
  for ($y = 0; $y -lt $height; $y += 2) {
    for ($x = 0; $x -lt $width; $x += 2) {
      $c = $bmp.GetPixel($x, $y)
      if ($c.R -lt 245 -or $c.G -lt 245 -or $c.B -lt 245) { $ink++ }
    }
  }
  $g.Dispose(); $bmp.Dispose()
  # The rectangle goes out through a variable, not through the output stream: anything a
  # function writes to the pipeline becomes part of its "return value", so a diagnostic
  # line printed here turned the ink count into a two-element array and every later
  # comparison failed.
  $script:lastInkRect = '' + $left + ',' + $top + ' ' + $width + 'x' + $height + ' (scale ' + $scale + ')'
  return $ink
}
function PickFolder($procId, [string]$directory) {
  # The one native dialog this program cannot avoid: the image-export folder box is
  # read-only, so only the picker can fill it. Its UIA children bridge through MSAA as
  # bare Panes with no patterns at all, so nothing can be written through UIA. What works
  # is Win32: WM_SETTEXT to the Edit, then a POSTED (never sent -- SendMessage does not
  # return until the modal loop the click opens has finished) BM_CLICK to the child button
  # whose control id is 1. Match that button by id, not by its text.
  $pk = $null
  for ($k = 1; $k -le 16; $k++) {
    Start-Sleep -Milliseconds 500
    foreach ($w in @($AE::RootElement.FindAll($TS::Children, (New-Object $PC($AE::ProcessIdProperty, $procId))))) {
      foreach ($e in (@($w) + @(FindType $w $CT::Window))) {
        if ($e.Current.Name -like '*Folder*' -or $e.Current.Name -like ('*' + (CjStr 0x6587,0x4EF6,0x5939) + '*')) { $pk = $e }
      }
    }
    if ($pk) { Say ('  picker appeared after ' + [math]::Round($k * 0.5, 1) + 's'); break }
  }
  if (-not $pk) { Say '  no picker window within 8s'; return $false }
  $kids = New-Object System.Collections.Generic.List[IntPtr]
  $cb = [W32+EnumProc] { param($h, $l) $kids.Add($h); return $true }
  [W32]::EnumChildWindows([IntPtr]$pk.Current.NativeWindowHandle, $cb, [IntPtr]::Zero) | Out-Null
  $box = $null; $ok = $null
  foreach ($h in $kids) {
    $cls = [W32]::Cls($h); $id = [W32]::GetDlgCtrlID($h)
    if ($cls -eq 'Edit' -and -not $box) { $box = $h }
    if ($cls -eq 'Button' -and $id -eq 1) { $ok = $h }
  }
  if (-not ($box -and $ok)) { Say '  picker has no Edit / no button with id 1'; return $false }
  [W32]::SendMessageW($box, 0x000C, [IntPtr]0, $directory) | Out-Null   # WM_SETTEXT
  Start-Sleep -Milliseconds 600
  [W32]::PostMessageW($ok, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null   # BM_CLICK
  Start-Sleep -Milliseconds 1800
  return $true
}
function PickFile($procId, [string]$fileName) {
  # The native *open* dialog, for the merge box's file list. Its UIA children are bare
  # Panes with no patterns, so the file is chosen by clicking its row in the folder view
  # (a real click at its rectangle) and then posting BM_CLICK to the button whose control
  # id is 1. Acceptance is read from *our own* dialog afterwards, never from the picker.
  # Every candidate window is printed first: a filter over rectangles that were never
  # looked at is how three rounds of "the dialog never opened" happened.
  # Match the native dialog by CLASS, not by title: the caption here is our own
  # translatable string ("PDFs to merge in"), so a needle like "Open" is wrong in both
  # languages. Qt's own dialogs report their widget class name (MergePagesDialog), which
  # makes '#32770' a precise test for "this one is the shell's".
  $pk = $null
  for ($k = 1; $k -le 16; $k++) {
    Start-Sleep -Milliseconds 500
    $names = @()
    foreach ($w in @($AE::RootElement.FindAll($TS::Children, (New-Object $PC($AE::ProcessIdProperty, $procId))))) {
      foreach ($e in (@($w) + @(FindType $w $CT::Window))) {
        $nm = $e.Current.Name
        $names += ('|' + $nm + '|' + $e.Current.ClassName + ' ' + [int]$e.Current.BoundingRectangle.Width + 'x' + [int]$e.Current.BoundingRectangle.Height)
        if ($e.Current.ClassName -eq '#32770') { $pk = $e }
      }
    }
    if ($pk) { Say ('  native dialog after ' + [math]::Round($k * 0.5, 1) + 's [' + $pk.Current.Name + ']'); break }
    if ($k -eq 4) { foreach ($n in ($names | Select-Object -Unique)) { Say ('  window candidate ' + $n) } }
  }
  if (-not $pk) { Say '  no native dialog within 8s'; return $false }
  $row = $null
  foreach ($e in @(FindType $pk $CT::ListItem)) {
    if ($e.Current.Name -like ('*' + $fileName + '*')) { $row = $e }
  }
  if (-not $row) {
    Say ('  no ListItem named [' + $fileName + '] in the picker; rows seen:')
    foreach ($e in @(FindType $pk $CT::ListItem)) { Say ('    row |' + $e.Current.Name + '|') }
    return $false
  }
  ClickAt $row
  Start-Sleep -Milliseconds 500
  $kids = New-Object System.Collections.Generic.List[IntPtr]
  $cb = [W32+EnumProc] { param($h, $l) $kids.Add($h); return $true }
  [W32]::EnumChildWindows([IntPtr]$pk.Current.NativeWindowHandle, $cb, [IntPtr]::Zero) | Out-Null
  $ok = $null
  foreach ($h in $kids) {
    if ([W32]::Cls($h) -eq 'Button' -and [W32]::GetDlgCtrlID($h) -eq 1) { $ok = $h }
  }
  if (-not $ok) { Say '  picker has no button with control id 1'; return $false }
  [W32]::PostMessageW($ok, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 1500
  return $true
}
function Start-ProbeApp([string]$language) {
  # Kill everything first: a leftover instance swallows the launch (single-instance
  # forwarding), and the run then measures a window with no document in it.
  Get-Process -Name xyzpdf -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
  Start-Sleep -Seconds 3
  if (-not (Test-Path $scratch)) { New-Item -ItemType Directory -Path $scratch | Out-Null }
  $ini = Join-Path $scratch 'language.ini'
  $text = "[appearance]`nlanguage=$language`n"
  if ($LastDir) { $text += "[files]`nlastOpenDirectory=$LastDir`n" }
  $text | Out-File -Encoding ASCII $ini
  $env:XYZPDF_SETTINGS_PATH = $ini
  $p = Start-Process -FilePath $Exe -ArgumentList $Doc -PassThru
  for ($sec = 1; $sec -le 20; $sec++) {
    Start-Sleep -Seconds 1
    if (Window $p.Id) { return $p }
  }
  Say 'ABORT: no document window within 20s (a window titled just "XYZPDF" means the launch was forwarded to a dying instance)'
  return $null
}

if (-not (Test-Path $Doc)) { Say ("no document at $Doc -- pass -Doc <path to a pdf>"); exit 1 }
if ($Log) { $lines.Clear() }
$app = Start-ProbeApp $Language
if (-not $app) { exit 1 }
Say ('window [' + (Window $app.Id).Current.Name + '] pid ' + $app.Id + ' language=' + $Language)

switch ($Mode) {
  'inventory' {
    # Every non-empty Name, tagged with its control type. Read the whole list rather than
    # grepping it for the sentences you expect: a needle list that omits a line is not
    # evidence that the line is absent.
    $seen = @{}
    foreach ($e in @(FindType (Window $app.Id) $CT::Window)) {
      Say ('window |' + $e.Current.Name + '|')
    }
    foreach ($e in @((Window $app.Id).FindAll($TS::Descendants, $ANY))) {
      $nm = $e.Current.Name
      if ($null -eq $nm) { continue }
      $nm = $nm.Replace("`r", ' ').Replace("`n", ' ').Trim()
      if ($nm.Length -lt 2) { continue }
      $key = $e.Current.ControlType.ProgrammaticName.Replace('ControlType.', '') + ' |' + $nm + '|'
      if ($seen.ContainsKey($key)) { continue }
      $seen[$key] = 1
      Say ('  ' + $key + (' enabled=' + $e.Current.IsEnabled))
    }
    Say ('  ' + $seen.Keys.Count + ' distinct names')
  }
  'menudump' {
    # Expand() reads a Qt menu with no synthesized input at all. It has been reliable for
    # the English menu bar and unreliable for the Chinese one (no submenu rows appear), so
    # a missing row in Chinese says nothing until the same script is run with -Language en
    # and does show rows.
    TopMenu $app.Id $Menu | Out-Null
    $rows = @()
    foreach ($e in @($AE::RootElement.FindAll($TS::Descendants, (New-Object $PC($AE::ProcessIdProperty, $app.Id))))) {
      if ($e.Current.ControlType.ProgrammaticName -eq 'ControlType.MenuItem') { $rows += $e }
    }
    Say ('  ' + $rows.Count + ' menu items visible after expanding [' + $Menu + ']')
    foreach ($e in $rows) { Say ('  row |' + $e.Current.Name + '| enabled=' + $e.Current.IsEnabled) }
  }
  'exportstatus' {
    # The export's receipt line, read the way sec.37 measured it: the StatusBar element's
    # Name and a crop of the message area, sampled at the same instants.
    if (-not (Test-Path $OutDir)) { Say "no such -OutDir: $OutDir"; exit 1 }
    # Only the names this program writes are cleared (<document name> <nnn>.png) -- the
    # folder is somewhere the reader may keep real pictures.
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($Doc)
    Get-ChildItem $OutDir -Filter *.png -ErrorAction SilentlyContinue |
      Where-Object { $_.Name.StartsWith($stem) } | Remove-Item -Force
    $ink0 = StatusInk $app.Id (Join-Path $scratch 'status-before.png')
    Say ('before: name=[' + (StatusName $app.Id) + '] ink=' + $ink0)
    TopMenu $app.Id 'File' | Out-Null
    $row = MenuRow $app.Id 'Export as Images'
    if (-not $row) { Say '  no Export as Images row'; exit 1 }
    ClickAt $row
    Start-Sleep -Milliseconds 1600
    $dlg = Dialog $app.Id 'Export as Images'
    if (-not $dlg) { Say '  the export dialog did not open'; exit 1 }
    $radio = Elem $dlg $CT::RadioButton 'Current page'
    if (-not $radio) { $radio = Elem $dlg $CT::RadioButton (CjStr 0x5F53,0x524D) }
    if ($radio) { ClickAt $radio; Start-Sleep -Milliseconds 400; Say ('  picked |' + $radio.Current.Name + '|') }
    $browse = Elem $dlg $CT::Button 'Browse'
    if (-not $browse) { $browse = Elem $dlg $CT::Button (CjStr 0x6D4F,0x89C8) }
    ClickAt $browse
    if (-not (PickFolder $app.Id $OutDir)) { exit 1 }
    $go = Elem (Dialog $app.Id 'Export as Images') $CT::Button 'Export'
    if (-not $go) { $go = Elem (Dialog $app.Id 'Export as Images') $CT::Button (CjStr 0x5BFC,0x51FA) }
    if (-not ($go -and $go.Current.IsEnabled)) { Say '  Export is still greyed'; exit 1 }
    ClickAt $go
    $shot = Join-Path $scratch 'status-message.png'
    for ($k = 1; $k -le 14; $k++) {
      Start-Sleep -Milliseconds 700
      $written = @(Get-ChildItem $OutDir -Filter *.png).Count
      $name = StatusName $app.Id
      $ink = StatusInk $app.Id ''
      if ($name -and -not (Test-Path $shot)) { $ink = StatusInk $app.Id $shot }
      Say ('  t=' + [math]::Round($k * 0.7, 1) + 's files=' + $written + ' ink=' + $ink `
           + ' name=[' + $name + '] at ' + $script:lastInkRect)
      if ($k -ge 11 -and $ink -lt $ink0) { break }
    }
    Say ('  crops kept at ' + (Join-Path $scratch 'status-before.png') + ' and ' + $shot)
  }
  'mergedonor' {
    # The merge box is the one edit whose undo row names a FILE, and the only way to reach
    # it is through the native open dialog -- which is why this row, unlike the other five,
    # had never been read on screen. Two donors, so both numerus branches come out of one
    # run: the row after the second merge is the newest operation.
    if (-not $LastDir) { Say 'mergedonor needs -LastDir (the folder holding -Donor)'; exit 1 }
    $undo = {
      param([string]$tag)
      TopMenu $app.Id 'Document' | Out-Null
      foreach ($e in @($AE::RootElement.FindAll($TS::Descendants, (New-Object $PC($AE::ProcessIdProperty, $app.Id))))) {
        if ($e.Current.ControlType.ProgrammaticName -eq 'ControlType.MenuItem' `
            -and $e.Current.Name -like 'Undo*') { Say ('  ' + $tag + 'row |' + $e.Current.Name + '|') }
      }
    }
    & $undo 'before: '
    foreach ($donor in @($Donor, 'donor2.pdf')) {
      TopMenu $app.Id 'Document' | Out-Null
      $row = MenuRow $app.Id 'Merge Files In'
      if (-not $row) { Say "  no Merge Files In row"; exit 1 }
      ClickAt $row
      Start-Sleep -Milliseconds 1600
      $dlg = Dialog $app.Id 'Merge Files In'
      if (-not $dlg) {
        Say '  merge dialog not found by title; windows in the subtree:'
        foreach ($w in @(FindType (Window $app.Id) $CT::Window)) { Say ('    |' + $w.Current.Name + '|') }
        exit 1
      }
      $add = Elem $dlg $CT::Button 'Add Files'
      if (-not $add) { $add = Elem $dlg $CT::Button (CjStr 0x6DFB,0x52A0,0x6587,0x4EF6) }
      ClickAt $add
      if (-not (PickFile $app.Id $donor)) { exit 1 }
      $dlg2 = Dialog $app.Id 'Merge Files In'
      foreach ($e in @(FindType $dlg2 $CT::DataItem)) { Say ('  table row |' + $e.Current.Name + '|') }
      foreach ($e in @(FindType $dlg2 $CT::Text)) {
        if ($e.Current.Name.Length -gt 12) { Say ('  text |' + $e.Current.Name.Replace("`n", ' / ') + '|') }
      }
      $go = Elem $dlg2 $CT::Button 'Merge these files'
      if (-not $go) { $go = Elem $dlg2 $CT::Button (CjStr 0x5408,0x5E76) }
      if (-not ($go -and $go.Current.IsEnabled)) { Say '  accept still greyed'; exit 1 }
      ClickAt $go
      Start-Sleep -Milliseconds 1200
      & $undo ($donor + ': ')
    }
    Say ('  status: [' + (StatusName $app.Id) + ']')
  }
}

Get-Process -Name xyzpdf -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
Say 'DONE'
