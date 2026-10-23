<#
    gui-probe.ps1 -- the UI Automation routes this project has actually measured, in one
    tracked file. Before this, every real-window pass was written from scratch in a temp
    directory against a script that no longer exists, and the routes had to be re-derived
    round by round (three rounds were lost on 2026-09-27 to "the export line never
    appears", which was only ever a wrong property read -- see Get-StatusName).

    It drives the *dev* build by default. Most modes need no keyboard and no clipboard: menu
    rows come from ExpandCollapse, our own Qt fields are read as UIA properties, and the one
    native dialog in the way is driven with Win32 messages. The five modes added on 2026-10-22
    (propsdialog, findwhole, snapshot, strippreview, mnemonics) do use synthesized keys, and
    snapshot also owns the clipboard; all five verify the foreground before typing, because a
    key sent to somebody else's window is not a reading.

    Modes:
      inventory      every non-empty Name in the window, with its control type. This is
                     the dump that caught two product defects the gate could not see.
      menudump       expand one top-level menu and print its rows (-Menu Document).
      exportstatus   run the image export through the real picker and read the status line back
                     two ways at once (-OutDir must exist; it writes files there).
      mergedonor     merge a one-page then a two-page donor through the native open dialog and
                     read the undo row after each, which is the only row whose text names a file
                     (-LastDir must hold -Donor; nothing is saved, the run ends by killing itself).
      splitsagain    record a split, reopen the box and change the size, which is the only way the
                     "this replaces the earlier one" aside exists at all -- twice, so both numerus
                     branches of it come out (nothing is saved here either).
      saverename     the save-changes-as row -> the native save box -> a name typed into its file
                     field, then the receipts read back from the file, the tab, the status line and
                     the source hash (sec.44; the route is in that mode's own comment, all of it
                     measured).
      langswitch     switch the interface language through the app's OWN Language menu and read the
                     result after a restart, then put it back. This is the only route to an English
                     installed copy: a packaged process never sees XYZPDF_SETTINGS_PATH (sec.48), and
                     its stored preference has no outside read channel at all -- so every reading here
                     comes back through the app's own settings dialog.
      formpage       crop the page area and count ink per horizontal band, which is how "does this
                     reader draw a form field" becomes a subtraction between two runs instead of a
                     look (sec.54; run it once on the plain file and once on the same page with a
                     field on it).
      openpicker       launch, raise the window by its own title bar, open the native open dialog and
                     print the rows it shows -- which is how "where does our own start directory put the
                     dialog, and does the shell's per-application memory matter" was judged (sec.62). The
                     dialog's folder is never exposed as a readable Name, so the FOLDER is read from its
                     contents. It raises the window by its title bar first because that is what the earlier
                     modes that work with pixels do -- but do NOT read that as the cure for the rounds where
                     nothing lands: sec.62 recorded four dead rounds, then two that worked with this raise in
                     place, then this same mode failing three tries again minutes later. Whether synthesised
                     input lands at all is per-round (sec.41); the raise is a precaution, and the reason this
                     mode prints the row it aims at is so a dead round leaves a reading behind.
      uacache        read one status-bar message four ways at the same instant -- a HELD element, a
                     freshly found one, one found from the desktop root, and the pixels -- because
                     sec.37 blamed a lag on the UIA client's property cache and never measured it.
      outlinetree    open the reading sidebar and count what the bookmark tree on screen actually holds:
                     TreeItem descendants, the root's own children, and how many names start with a digit
                     -- that last one because a root-only read cannot produce a digit-starting name at all,
                     so it separates "the nesting is on screen" from "there are just more rows". sec.64 fixed
                     the tree read with file-level evidence only; this is the screen half of it, and it opens
                     the sidebar through its own menu row and reads that row's TogglePattern state on both
                     sides of the click rather than assuming the panel came up.
      tabstrip       crop the document tab strip to a PNG, which is the only way to see what its
                     buttons are drawn as. A stylesheet rule can point at any picture it likes and
                     every count in this harness stays green -- sec.66 found the tab's close button
                     showing the combo box's down arrow that way, and no number could have said so.
      bookdialog     open the bookmark editor from the Document menu and walk it to the end: the
                     table's UIA shape (a QTableWidget has never been read by this harness, and
                     sec.65's QTreeView result says the shape is unknown until measured), the cells
                     read one by one -- three consecutive DataItem names are one row's title, level
                     and page, which is how sec.72 caught the level column being silent and sec.74
                     checks it is not -- whether the accept button really arrives greyed on an
                     untouched box, what it says after a row is added, the refusal typed into a cell
                     through its own Value pattern, and which row the menu shows once the request is
                     recorded. exec() blocks, so this is the only route through
                     menu -> dialog -> setOutline that any check has ever taken.
      propsdialog    press Ctrl+D and read the properties window back: its class, the file name it
                     states, the Pages label and a standalone number that must equal the count the
                     main window's own status bar shows. -Control expects one page too many, so the
                     page-count verdict must exit 1. Needs tests/fixtures/many-pages.pdf (40 pages).
      findwhole      type PDF into the find bar, then press the Whole words box with a REAL click and
                     require the count to go from two hits to none (text.pdf holds XYZPDF twice).
                     -Control clicks Match case instead, which changes neither, and exits 1.
      snapshot       press Ctrl+Shift+5, drag across picture.pdf, and require three things to agree:
                     the toolbar toggle is On, the status line names W x H, and the clipboard image
                     has that exact size and carries the page's red block. -Control skips the
                     shortcut and exits 1. NOTE: this overwrites the user's clipboard.
      strippreview   open the sidebar, measure the first thumbnail's red block, rotate the current
                     page through the dialog, then require the strip tab to read "Thumbnails
                     (preview of changes)" and the block to turn portrait. -Control skips the
                     rotation and exits 1. The page itself is white on white, so the red block is the
                     measurable half of "the thumbnail is now landscape".
      mnemonics      press Alt+letter for every top-level menu (f/e/v/d/g/w/h in English,
                     f/e/v/d/g/n/h in Chinese) and require exactly that item expanded; the other
                     language's Window letter and l must expand nothing. -Control skips the Go press
                     and exits 1.
      translatesetup press Ctrl+A then Ctrl+T and read the box that answers when no translation
                     service is configured: its two lines, its class (QMessageBox, which no
                     translation changes) and the Open Settings button. Press that button and the
                     app's OWN settings dialog must come up with the keyboard focus on the
                     translation-service combo box; the dialog is then printed with PrintWindow and
                     every row's rectangle required to lie inside its client area -- a box that grew
                     a row after its height was fixed is a layout fact only a picture can show
                     (sec.71, sec.110), so the numbers are asserted and the PNG is kept. -Control
                     keeps every step and then looks for the settings dialog WITHOUT pressing the
                     button, which must exit 1.
      xfanotice      one document per run, judged against -ExpectXfa packet|dynamic|absent: the
                     status line and the delete box must both carry the XFA sentence on a file that
                     carries a packet -- the two kinds have different status sentences -- and the
                     status line must carry NEITHER on a file with no XFA at all. See the mode's own
                     comment for why the status line is never judged on the bare word XFA.
      replynotice    the cross-page thread file, four readings and no accept button pressed: the
                     delete box names one orphaned reply for range 1 and says nothing for range 3;
                     the split box names one at 1 page per piece and nothing at 2 (pages 1 and 2 then
                     land in the same file). -Control keeps every step and expects the sentence at
                     range 3, where it cannot be.

    Examples (from the repository root):
      powershell -NoProfile -ExecutionPolicy Bypass -File xyzpdf/tools/gui-probe.ps1 -Mode inventory
      ... -Mode menudump -Menu Document
      ... -Mode exportstatus -OutDir H:/tmp/xyzpdf-probe
      ... -Mode mergedonor -LastDir H:/tmp/xyzpdf-probe -Donor donor1.pdf
      ... -Mode langswitch -Installed -To English
      ... -Mode outlinetree -Doc H:/tmp/outline/real-book.pdf -Log H:/tmp/outline/outlinetree.txt
      ... -Mode bookdialog -Doc H:/tmp/outline/real-book.pdf -Log H:/tmp/outline/bookdialog.txt
      ... -Mode propsdialog -Doc xyzpdf/tests/fixtures/many-pages.pdf
      ... -Mode findwhole -Doc xyzpdf/tests/fixtures/text.pdf
      ... -Mode snapshot -Doc xyzpdf/tests/fixtures/picture.pdf
      ... -Mode strippreview -Doc xyzpdf/tests/fixtures/picture.pdf
      ... -Mode mnemonics -Doc xyzpdf/tests/fixtures/text.pdf
      ... -Mode translatesetup -Doc xyzpdf/tests/fixtures/text.pdf
      ... -Mode xfanotice -Doc H:/tmp/f9-samples/field-xfa.pdf -ExpectXfa packet
      ... -Mode xfanotice -Doc H:/tmp/f9-samples/plain.pdf -ExpectXfa packet   (the control: exit 1)
      ... -Mode replynotice -Doc H:/tmp/f9-samples/thread-across-pages.pdf
      ... -Mode replynotice -Control -Doc H:/tmp/f9-samples/thread-across-pages.pdf   (also exit 1)
      ... -Mode findwhole -Control -Doc xyzpdf/tests/fixtures/text.pdf   (the control must exit 1)

    -Log takes a UTF-8 file, which is the only way to read a Chinese interface back from this
    console (it is GBK, so the window title alone arrives as mojibake). Write such a path with
    forward slashes: a backslashed one handed in from Git Bash loses its separators and the log
    lands as ./tmpfoo.txt in the repository root.
#>
param(
  [ValidateSet('inventory', 'menudump', 'exportstatus', 'mergedonor', 'splitsagain', 'saverename', 'saveinplace', 'closeprompt', 'saveas', 'langswitch', 'formpage', 'uacache', 'openpicker', 'outlinetree', 'tabstrip', 'bookdialog', 'deletesummary', 'pathforms', 'propsdialog', 'findwhole', 'snapshot', 'strippreview', 'mnemonics', 'translatesetup', 'xfanotice', 'replynotice')]
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
  # pathforms: the file the picker is aimed at. The launched instance keeps opening -Doc, so the two
  # names MUST differ -- if they were the same, "did our title change to the leaf?" would already be
  # true before the dialog is touched, which is the vacuous-check shape this suite keeps tripping on
  # (sec.77, sec.95). The mode refuses to run rather than print a table of tautologies.
  [string]$Target = '',
  # -Installed only: a packaged instance cannot be handed a file (no AppExecutionAlias, and
  # Start-Process cannot run the exe under WindowsApps), so a mode that needs a document has to
  # open one through the program's own picker. This is the name of the row to click in that
  # dialog -- the file name, not a path -- and the picker's own folder memory decides whether the
  # row is there (sec.62: the shell files that memory under the exe name, which dev and packaged
  # builds share). Empty means do not open anything, which is what every packaged mode ran before.
  [string]$OpenViaPicker = '',
  # langswitch: the row to pick in the Language menu, matched by its exact name. The language rows
  # are written in their own language -- "English" in either interface, and the Chinese one built
  # from code points in Get-LanguageNames -- because a picker that says "Japanese" is no use to
  # someone who cannot read Japanese. Only the first row, which names a behaviour rather than a
  # language, is translated.
  [string]$To = 'English',
  # Leave the language switched instead of putting it back. Off by default: the installed copy is
  # the reader's, and a probe that changes it has to change it back.
  [switch]$Keep,
  # CONTROL RUN for the five modes added on 2026-10-22. The rule in this project is that every
  # new assertion has to be shown able to fail (sec.99: a control that cannot change the outcome
  # proves the opposite of what it looks like). Those five modes are harness work, so their
  # control cannot be "break the product and restore it" -- the product must stay untouched.
  # Instead -Control takes a deliberately wrong route while keeping the SAME verdict: skip the
  # Ctrl+Shift+5 press, click Match case instead of Whole words, expect one page too many, skip
  # the rotation, skip one Alt key. Each of those must exit 1; a control that stays green means
  # the verdict was never looking at the screen. Those five read this switch, and so do the three
  # added for handoff-03 F9: translatesetup looks for the settings dialog without pressing the
  # button that opens it, xfanotice asks for the packet sentence on a file carrying no packet,
  # replynotice expects the orphan sentence on a page that holds neither end of the thread.
  [switch]$Control,
  # Run against the installed package instead of the build tree. It cannot be started by exe
  # path (WindowsApps blocks that) and takes no file argument (no AppExecutionAlias is declared),
  # so an installed run has to open its own document through the native picker first.
  [switch]$Installed,
  [string]$AppId = 'allgoodhi.XYZPDF_edpkq8hzfe5n4!XYZPDF',
  # deletesummary: what the delete box must do with the form sentence. 'present' on a document that
  # carries a field, 'absent' on one that does not -- the two halves of sec.77's rule, and the reason a
  # single run cannot prove the sentence is conditional. Anything else reports without judging.
  # A mismatch is a Bail, because sec.71 measured that a FAIL printed on a path that cannot set the
  # exit code is decoration, not a check.
  [string]$ExpectForm = 'none',
  # xfanotice: which XFA news this document must bring. 'packet' and 'dynamic' are the two status
  # sentences F3 added (an ordinary XFA form, and one that also asks the reader to draw its own
  # pages), and both also expect the delete box to carry the packet sentence; 'absent' is the
  # negative half -- plain.pdf must show neither, or the program warns on every file it opens.
  # This is also where the mode's control lives: -ExpectXfa packet on plain.pdf asks for a sentence
  # the file cannot produce, and that round must exit 1.
  [string]$ExpectXfa = 'none',
  [string]$Log = ''
)

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName WindowsBase
Add-Type -AssemblyName System.Drawing
# SendKeys and the clipboard. The modes that use them are the only ones that need a foreground
# window, and each of them checks the foreground before typing (a key sent to somebody else's
# window is not a reading). Windows PowerShell 5.1 starts in STA, which the clipboard needs.
Add-Type -AssemblyName System.Windows.Forms

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint d, int e, int extra, IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr h, bool alternate);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr hParent, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left, top, right, bottom; }
  public static RECT Rect(IntPtr h) { RECT r; GetWindowRect(h, out r); return r; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x, y; }
  // The client area's own top-left in screen pixels -- what a window-sized picture has to be offset
  // by before a UIA rectangle means anything inside it (sec.89).
  public static POINT ClientOrigin(IntPtr h) { POINT p = new POINT(); p.x = 0; p.y = 0; ClientToScreen(h, ref p); return p; }
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
  // The snapshot tool acts on a drag, not a click: a press, a path, a release. Twelve steps
  // because Qt delivers the intermediate moves as mouse events and the box is built from them;
  // a press and release at two points with nothing between has not been measured here.
  public static void Drag(int x1, int y1, int x2, int y2) {
    SetCursorPos(x1, y1);
    System.Threading.Thread.Sleep(120);
    mouse_event(0x0002, 0, 0, 0, IntPtr.Zero);
    System.Threading.Thread.Sleep(150);
    for (int i = 1; i <= 12; i++) {
      SetCursorPos(x1 + (x2 - x1) * i / 12, y1 + (y2 - y1) * i / 12);
      System.Threading.Thread.Sleep(25);
    }
    System.Threading.Thread.Sleep(150);
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
# A mode that gives up partway must leave nothing behind: a bare `exit` skips the kill at the end of
# this file, and the next run then launches a second instance whose document is forwarded to the stale
# one -- so it drives a window it did not open. sec.74 lost a round that way.
function Bail([string]$why) {
  Say ('  ' + $why)
  # A mode that gives up halfway is the mode that might have written something it should not have,
  # so the save guard is read here too -- and the exit code stays 1 either way.
  if ($script:guardArmed) { Guard-Check 'bailing mid-run' | Out-Null }
  Get-Process -Name xyzpdf -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
  if ($script:guardBroken) { Say '  GUARD: the file handed to the probe MOVED during this run (hashes above)' }
  exit 1
}
function CjStr([int[]]$codePoints) {
  # This file must stay pure ASCII: Windows PowerShell reads a BOM-less .ps1 as ANSI, so a
  # literal Chinese character is corrupt before the comparison runs. Match strings for the
  # Chinese interface are built from code points instead.
  -join ($codePoints | ForEach-Object { [char]$_ })
}
function Needle([string]$s) {
  # A run of hex code points ("6587,6863" for that word) lets a caller ask for a Chinese
  # label from the command line without this file holding a single non-ASCII byte.
  if ($s -match '^[0-9a-fA-F]+(,[0-9a-fA-F]+)*$') {
    return CjStr ($s.Split(',') | ForEach-Object { [Convert]::ToInt32($_, 16) })
  }
  return $s
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
  foreach ($w in @($AE::RootElement.FindAll($TS::Children, (New-Object $PC($AE::ProcessIdProperty, $procId))))) {
    if ($w.Current.Name -like '*XYZPDF*') { return $w }
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
function DialogAny($procId, [string[]]$needles, [switch]$Show) {
  # The same lookup with every spelling of the title, because the title is one of our own
  # translatable strings: a mode that asks for only the English half cannot run on the Chinese
  # interface at all, and its failure reads like a product defect (sec.84 found a needle that
  # was never executed; sec.87 found a whole mode that never could have been).
  # Two scopes, because a window-modal Qt dialog is not always reachable from inside the main
  # window's subtree -- sec.85 measured the delete dialog there, and sec.87's merge dialog was
  # not, with the subtree lookup returning nothing at all. Printing the candidates is what turns
  # "not found" from a dead end into a reading.
  $sub = @()
  $main = Window $procId
  if ($main) { $sub = @(FindType $main $CT::Window) }
  $tops = @($AE::RootElement.FindAll($TS::Children, (New-Object $PC($AE::ProcessIdProperty, $procId))))
  foreach ($n in $needles) {
    foreach ($e in ($sub + $tops)) {
      if ($e.Current.Name -like ('*' + $n + '*')) {
        if ($Show) { Say ('  matched [' + $e.Current.Name + '] class=' + $e.Current.ClassName) }
        return $e
      }
    }
  }
  if ($Show) {
    Say ('  no match in ' + $sub.Count + ' subtree window(s) or ' + $tops.Count + ' top-level window(s):')
    foreach ($e in ($sub + $tops)) {
      Say ('    [' + $e.Current.Name + '] class=' + $e.Current.ClassName)
    }
  }
  return $null
}
function MatchOne($root, $type, [string[]]$needles) {
  # Dialog labels come from the catalogue, so a mode that must run in either language has to
  # ask for both spellings of each one.
  foreach ($n in $needles) { $e = Elem $root $type $n; if ($e) { return $e } }
  return $null
}
function RowAny($procId, [string[]]$needles) {
  foreach ($n in $needles) { $e = MenuRow $procId $n; if ($e) { return $e } }
  return $null
}
function WaitDialog($procId, [string[]]$needles, [int]$rounds = 12) {
  # Poll, and require the candidate to be alive: a Window element that is on its way out
  # still matches by name for a moment, and then answers an empty Name and zero descendants.
  # Binding to one of those makes "the dialog shows no text" a stale handle, not a finding --
  # which is exactly what a Chinese run of splitsagain reported before this existed.
  for ($k = 1; $k -le $rounds; $k++) {
    foreach ($n in $needles) {
      foreach ($e in @(FindType (Window $procId) $CT::Window)) {
        if ($e.Current.Name -like ('*' + $n + '*') -and @($e.FindAll($TS::Children, $ANY)).Count -gt 0) { return $e }
      }
    }
    Start-Sleep -Milliseconds 400
  }
  return $null
}
function Elem($root, $type, [string]$needle) {
  foreach ($e in @(FindType $root $type)) { if ($e.Current.Name -like ('*' + $needle + '*')) { return $e } }
  return $null
}
function MenuItems($procId) {
  $found = @()
  foreach ($e in @($AE::RootElement.FindAll($TS::Descendants, (New-Object $PC($AE::ProcessIdProperty, $procId))))) {
    if ($e.Current.ControlType.ProgrammaticName -eq 'ControlType.MenuItem') { $found += $e }
  }
  return $found
}
function TopMenu($procId, $which) {
  # $which may be one label or a list of them, because the bar itself is translatable.
  $needles = @($which)
  # ExpandCollapse.Expand() opens a Qt menu with no synthesized input, and it has been reliable
  # for the English bar and unreliable for the Chinese one (sec.34: four attempts, no rows came
  # back). A real click at the item's own rectangle is the route that has never failed here, so
  # try Expand first and fall back -- and say which one produced rows, because an empty list
  # from a channel that never worked reads exactly like a menu with nothing in it.
  $before = @(MenuItems $procId).Count
  foreach ($bar in @(FindType (Window $procId) $CT::MenuBar)) {
    foreach ($t in @($bar.FindAll($TS::Children, $ANY))) {
      $hit = $false
      foreach ($n in $needles) { if ($t.Current.Name -like ('*' + $n + '*')) { $hit = $true } }
      if ($hit) {
        $xp = Pat $t 'ExpandCollapse'
        if ($xp) { try { $xp.Expand() } catch { Say ('  Expand threw: ' + $_.Exception.Message) } }
        for ($k = 1; $k -le 4; $k++) {
          Start-Sleep -Milliseconds 300
          if (@(MenuItems $procId).Count -gt $before) { Say ('  opened [' + $t.Current.Name + '] via Expand'); return $t }
        }
        ClickAt $t
        for ($k = 1; $k -le 6; $k++) {
          Start-Sleep -Milliseconds 300
          if (@(MenuItems $procId).Count -gt $before) { Say ('  opened [' + $t.Current.Name + '] via a real click'); return $t }
        }
        Say ('  [' + $t.Current.Name + '] found, but neither Expand nor a click produced rows')
        return $t
      }
    }
  }
  Say ('  no top-level menu matching [' + ($needles -join ',') + ']')
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
  # Did it actually take? Returning true because a click was posted is what let sec.88's export rounds
  # sample fourteen zeros and still say DONE: the box was still open, refusing the path it was handed.
  # So ask whether the picker is gone, and print what it says when it is not.
  for ($k = 1; $k -le 6; $k++) {
    $still = $null
    foreach ($w in @($AE::RootElement.FindAll($TS::Children, (New-Object $PC($AE::ProcessIdProperty, $procId))))) {
      foreach ($e in (@($w) + @(FindType $w $CT::Window))) {
        if ($e.Current.Name -like '*Folder*' -or $e.Current.Name -like ('*' + (CjStr 0x6587, 0x4EF6, 0x5939) + '*')) { $still = $e }
      }
    }
    if (-not $still) { return $true }
    Start-Sleep -Milliseconds 500
  }
  Say '  the folder picker is STILL open after the click -- the path was refused, nothing was picked'
  foreach ($t in @(FindType $still $CT::Text)) {
    if ($t.Current.Name.Length -gt 1) { Say ('    it says |' + $t.Current.Name.Replace("`r", ' ').Replace("`n", ' / ') + '|') }
  }
  return $false
}
function NameField($handles) {
  # Which Edit is the file-name field is per dialog flavour, and getting it wrong is what made
  # sec.28 and sec.29 disagree about "the name box": in the save dialog it is control id 1001,
  # in the open dialog it is 1148 (the Edit inside the ComboBoxEx32), and id 1001 there is the
  # address ToolbarWindow32. So: prefer 1001, then the ComboBoxEx32's 1148, then any Edit.
  $any = $null
  foreach ($h in $handles) {
    if ([W32]::Cls($h) -ne 'Edit') { continue }
    $id = [W32]::GetDlgCtrlID($h)
    if ($id -eq 1001) { return $h }
    if ($id -eq 1148) { $any = $h }
    if (-not $any) { $any = $h }
  }
  return $any
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
  # Two routes, in this order, because which one is available depends on where the box opened.
  #   row click -- proven since sec.29: click the ListItem naming the file, then BM_CLICK to the
  #                button with control id 1. Needs the folder to already hold that file.
  #   typed path -- WM_SETTEXT of the full path into the File name box (id 1148), same accept
  #                button. Re-measured 2026-10-11: a note here used to say a typed path "leaves the
  #                dialog open" (2026-09-28), but that test wrote id 1001, which in THIS dialog is
  #                the address toolbar, not the name box. Given a bare name this route is not tried.
  $row = $null
  foreach ($e in @(FindType $pk $CT::ListItem)) {
    if ($e.Current.Name -like ('*' + [System.IO.Path]::GetFileName($fileName) + '*')) { $row = $e }
  }
  if (-not $row) {
    Say ('  no ListItem named [' + $fileName + '] in the picker; rows seen:')
    foreach ($e in @(FindType $pk $CT::ListItem)) { Say ('    row |' + $e.Current.Name + '|') }
    if ($fileName -notmatch '[\\:]') { Say '  (a bare name was given, so no path to type)'; return $false }
    Say '  no row, so trying the typed-path route (File name box id 1148)'
    if (-not (TypePathIntoPicker $pk $fileName)) { return $false }
  } else {
    ClickAt $row
    Start-Sleep -Milliseconds 500
  }
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
function LanguageNames() {
  # Every label the interface can show for a language. The native names come from
  # Languages::available() and are NOT translated -- that is the point of the picker -- so the same
  # strings have to be asked for in either interface. Only the first row names a behaviour rather
  # than a language, and that one goes through tr(): "Follow the system" / its source form.
  return @((CjStr 0x7B80,0x4F53,0x4E2D,0x6587), 'English', 'Follow the system', (CjStr 0x8DDF,0x968F,0x7CFB,0x7EDF))
}
function LanguageLabel($procId, [string]$tag) {
  # The stored language read back THROUGH THE APP, because there is no outside read channel on a
  # packaged run: the AppContainer storage key for this package holds only ResourcesConfig, the real
  # values live in the Helium hive (sec.48), and HKCU:\Software\xyzpdf is the dev build's own store
  # and never moves. The settings dialog prints Languages::nativeName(AppSettings::languageCode())
  # on one of its buttons, so the interface reports its own setting.
  TopMenu $procId @('Help', (CjStr 0x5E2E,0x52A9)) | Out-Null
  $row = RowAny $procId @('Settings', (CjStr 0x8BBE,0x7F6E))
  if (-not $row) { Say '  no Settings row in the Help menu'; return '' }
  ClickAt $row
  $dlg = WaitDialog $procId @('Settings', (CjStr 0x8BBE,0x7F6E))
  if (-not $dlg) { Say '  the settings dialog did not appear'; return '' }
  # Printed before filtered, as always: a needle list that omits a label is not evidence the label
  # is absent, and this dialog's buttons are the only place the language name is drawn.
  Say ('  ' + $tag + ': buttons in [' + $dlg.Current.Name + ']')
  $found = ''
  foreach ($b in @(FindType $dlg $CT::Button)) {
    $n = $b.Current.Name
    Say ('    button |' + $n + '|')
    if (($found -eq '') -and ((LanguageNames) -contains $n)) { $found = $n }
  }
  # Cancel, not OK: reject() puts back whatever the dialog had staged, accepted() would not.
  $cancel = MatchOne $dlg $CT::Button @('Cancel', (CjStr 0x53D6,0x6D88))
  if ($cancel) { ClickAt $cancel } else { Say '  no Cancel button, leaving the dialog open' }
  Start-Sleep -Milliseconds 800
  if ($found -eq '') { Say '  no button named after a language' }
  return $found
}
function MenuBarNames($procId) {
  # The bar is what a language switch is actually judged by: every top-level entry is tr()'d, and
  # one of them is deliberately not.
  $out = @()
  foreach ($mb in @(FindType (Window $procId) $CT::MenuBar)) {
    foreach ($t in @($mb.FindAll($TS::Children, $ANY))) { $out += $t.Current.Name }
  }
  return $out
}
function DismissNotice($procId, [string]$tag) {
  # What the language change answers with. Its title is QStringLiteral("Language") and its body is
  # one of our tr() strings, so this single window shows both kinds of string at once -- the
  # cheapest possible proof that a catalogue is actually loaded in the build being measured.
  $note = WaitDialog $procId @('Language')
  if (-not $note) { Say ('  ' + $tag + ': no notice window appeared'); return $false }
  Say ('  ' + $tag + ': notice window [' + $note.Current.Name + '] class=' + $note.Current.ClassName)
  $texts = @(FindType $note $CT::Text)
  Say ('    ' + $texts.Count + ' Text elements')
  foreach ($t in $texts) { Say ('    text |' + $t.Current.Name + '|') }
  $ok = MatchOne $note $CT::Button @('OK', 'Close', (CjStr 0x786E,0x5B9A), (CjStr 0x5173,0x95ED))
  if (-not $ok) {
    foreach ($b in @(FindType $note $CT::Button)) { Say ('    button |' + $b.Current.Name + '|') }
    Say '    no accept button this script knows'
    return $false
  }
  Say ('    accept button |' + $ok.Current.Name + '|')
  ClickAt $ok
  Start-Sleep -Milliseconds 900
  return $true
}
function LanguageRows($procId, [string]$tag) {
  # The Language popup with each row's check state. A checkable QAction reaches UIA as TogglePattern,
  # so which language is live is READ rather than inferred -- which matters twice over here: the
  # checkmark is the only sign on screen of the chosen row, and a screen-reader user has nothing else.
  # The bar's own entries are also ControlType.MenuItem, so the bar has to be subtracted or this
  # list claims eight languages.
  #
  # It LEAVES THE MENU OPEN and returns the rows: the click has to go to one of these elements.
  # TopMenu cannot be run twice on an open menu -- its test for "rows came back" is that the item
  # count GREW, which an open menu cannot do, and the fallback real click then closes it. That is
  # how the first run of this mode lost its own row.
  $barNames = MenuBarNames $procId
  $n0 = @(MenuItems $procId).Count
  TopMenu $procId 'Language' | Out-Null
  $rows = @(@(MenuItems $procId) | Where-Object { $barNames -notcontains $_.Current.Name })
  Say ('  ' + $tag + ': ' + $rows.Count + ' rows under [Language] (' + $n0 + ' items before, bar is ' + $barNames.Count + ')')
  foreach ($e in $rows) {
    $state = '(no TogglePattern)'
    $tg = Pat $e 'Toggle'
    if ($tg) { try { $state = $tg.Current.ToggleState.ToString() } catch { $state = 'threw' } }
    Say ('    row |' + $e.Current.Name + '| enabled=' + $e.Current.IsEnabled + ' toggle=' + $state)
  }
  return $rows
}
function LanguageNeedles([string]$s) {
  # The language rows read the same in either interface ("English", the Chinese name in its own
  # characters), but the FIRST row names a behaviour and is translated: "Follow the system" in an
  # English interface, its source form in a Chinese one. So a caller who asks for either spelling
  # gets both to match against -- sec.92 measured exactly this failing: asking for the Chinese
  # spelling of that row (as code points) found nothing while the installed copy was still in
  # English, and the restore bailed and left the reader's copy switched.
  if ($s -eq (CjStr 0x8DDF,0x968F,0x7CFB,0x7EDF) -or $s -eq 'Follow the system') {
    return @('Follow the system', (CjStr 0x8DDF,0x968F,0x7CFB,0x7EDF))
  }
  return @($s)
}

function ClickLanguageRow($rows, [string[]]$names, [string]$tag) {
  # Rows are matched by EXACT name against a list: the language rows are written in their own
  # language and so read the same in either interface, but the first one names a behaviour rather
  # than a language and is translated -- "Follow the system" in English, its source form in Chinese.
  foreach ($e in $rows) {
    foreach ($n in $names) {
      if ($e.Current.Name -eq $n) {
        Say ('  ' + $tag + ': clicking |' + $e.Current.Name + '|')
        ClickAt $e
        Start-Sleep -Milliseconds 900
        return $true
      }
    }
  }
  Say ('  ' + $tag + ': nothing among the ' + $rows.Count + ' rows is named [' + ($names -join ', ') + ']')
  return $false
}
function ParkCursorAway() {
  # Put the pointer in a screen corner BEFORE the app is launched. A toolbar button's statusTip is
  # written into the same status-bar slot as a showMessage() line and it replaces it outright -- the
  # timed message's timer is killed, so it does not come back when the hover ends. Where the last
  # round left its cursor decided what the next round could read: the second xfanotice round found
  # "Drag a box and recognize the text in the pixels inside it" where the XFA sentence should have
  # been, which is a reading of a mouse position and not of the product. Every mode that reads a
  # status line needed this; only the ones that happen to have started with the cursor elsewhere
  # ever got an answer.
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  [W32]::SetCursorPos([int]($b.Width - 6), [int]($b.Height - 6)) | Out-Null
  Start-Sleep -Milliseconds 200
}

function Start-ProbeApp([string]$language) {
  # Kill everything first: a leftover instance swallows the launch (single-instance
  # forwarding), and the run then measures a window with no document in it.
  Get-Process -Name xyzpdf -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
  Start-Sleep -Seconds 3
  ParkCursorAway
  if (-not (Test-Path $scratch)) { New-Item -ItemType Directory -Path $scratch | Out-Null }
  $ini = Join-Path $scratch 'language.ini'
  $text = "[appearance]`nlanguage=$language`n"
  if ($LastDir) { $text += "[files]`nlastOpenDirectory=$LastDir`n" }
  $text | Out-File -Encoding ASCII $ini
  if ($Installed) {
    # The packaged copy ignores XYZPDF_SETTINGS_PATH (the environment never reaches a process
    # the shell starts), so its language is the system one and its own settings store is used.
    Start-Process -FilePath 'explorer.exe' -ArgumentList ('shell:AppsFolder' + [char]92 + $AppId)
    for ($sec = 1; $sec -le 25; $sec++) {
      Start-Sleep -Seconds 1
      $cands = @(Get-Process -Name xyzpdf -ErrorAction SilentlyContinue)
      foreach ($c in $cands) { if (Window $c.Id) { return $c } }
    }
    Say 'ABORT: the installed app did not come up with a window'
    return $null
  }
  $env:XYZPDF_SETTINGS_PATH = $ini
  $p = Start-Process -FilePath $Exe -ArgumentList $Doc -PassThru
  for ($sec = 1; $sec -le 20; $sec++) {
    Start-Sleep -Seconds 1
    if (Window $p.Id) { return $p }
  }
  Say 'ABORT: no document window within 20s (a window titled just "XYZPDF" means the launch was forwarded to a dying instance)'
  return $null
}

function Restart-ProbeApp([string]$language) {
  # A language change is promised for the NEXT start -- that is what the notice says -- so a mode
  # that judges one has to actually restart. Same kill-everything-then-poll rule as the first
  # launch: a leftover instance swallows the launch, and a fixed sleep has been seen to come back
  # holding an empty window (sec.34).
  Get-Process -Name xyzpdf -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
  Start-Sleep -Seconds 3
  return (Start-ProbeApp $language)
}

function PickerChildren($pk) {
  # Every HWND inside a native picker, in window order. Printed because a picker field has been
  # guessed wrong more than once in this suite: the File name box is control id 1148 (a
  # ComboBoxEx32/ComboBox/Edit chain that all report that id), while id 1001 -- the one that IS the
  # name box in the SAVE dialog (sec.44) -- is the address TOOLBAR here. WM_GETTEXT on that Edit
  # answers empty while the box shows text, so nothing about these fields can be read back.
  $kids = New-Object System.Collections.Generic.List[IntPtr]
  $cb = [W32+EnumProc] { param($h, $l) $kids.Add($h); return $true }
  [W32]::EnumChildWindows([IntPtr]$pk.Current.NativeWindowHandle, $cb, [IntPtr]::Zero) | Out-Null
  return $kids
}

function TypePathIntoPicker($pk, [string]$path) {
  # WM_SETTEXT of a full path into the open dialog's File name box. An older note here said a typed
  # path "leaves the dialog open" (measured 2026-09-28); that test wrote id 1001, which is the
  # address toolbar, so the sentence was about the wrong control. Re-measured 2026-10-11 against
  # 1148: the dialog accepts it and the document opens. Whether it worked is never decided here --
  # the caller reads its own receipt (our window title / our dialog), because this box cannot be
  # read back and a posted click is not an accepted dialog (sec.88).
  $kids = PickerChildren $pk
  foreach ($h in $kids) {
    Say ('    id=' + [W32]::GetDlgCtrlID($h) + ' class=' + [W32]::Cls($h) + ' text=[' + [W32]::Text($h) + ']')
  }
  # Backslashes, always. Measured as a triple on 2026-10-11 in the SAME box on the packaged copy:
  # the slash form of the same path left the dialog open and the round exited 1; the backslash form
  # opened it; and with the normalisation below the slash form opens it too. This is sec.88's finding
  # about the shell's FOLDER box (it refused a slash path with its own localised "invalid folder"
  # line, while our Qt directory box has accepted that spelling for slices) showing up a second time,
  # here in the file-name field. bash hands most of these paths over with slashes, so normalising here
  # is what keeps a caller's spelling from deciding whether a round can work.
  $fixed = $path.Replace('/', '\')
  if ($fixed -ne $path) { Say ('  slashes normalised for the shell: [' + $fixed + ']') }
  $edits = @($kids | Where-Object { [W32]::GetDlgCtrlID($_) -eq 1148 })
  if (-not $edits) {
    $edits = @($kids | Where-Object { $c = [W32]::Cls($_); ($c -eq 'Edit') -or ($c -eq 'ComboBox') })
  }
  if (-not $edits) { Say '  no Edit or ComboBox child to write the path into'; return $false }
  foreach ($h in $edits) {
    Say ('  WM_SETTEXT of the full path into id=' + [W32]::GetDlgCtrlID($h) + ' class=' + [W32]::Cls($h))
    [W32]::SendMessageW($h, 0x000C, [IntPtr]::Zero, $fixed) | Out-Null
  }
  return $true
}

function FindPicker([int]$procId) {
  # The shell dialog belonging to this process, matched by CLASS (#32770), not by caption -- the
  # caption is our own translatable string, so a needle like "Open" is wrong in one of the two
  # languages here (sec.45/sec.87). Returns $null when no such window exists, which is what tells a
  # caller the dialog actually closed.
  foreach ($w in @($AE::RootElement.FindAll($TS::Children, (New-Object $PC($AE::ProcessIdProperty, $procId))))) {
    foreach ($e in (@($w) + @(FindType $w $CT::Window))) {
      if ($e.Current.ClassName -eq '#32770') { return $e }
    }
  }
  return $null
}

function ShowOpenDialog([int]$procId) {
  # Bring up our own File / Open dialog and hand back the shell window, or $null. Extracted from
  # -Mode openpicker on 2026-10-15 so -Mode pathforms drives the SAME route rather than a second copy
  # of it that can drift into working in only one language (sec.87's lesson about lookups).
  # The window is raised by its own title bar first because a launch from AppsFolder does not take
  # the foreground by itself: sec.84 watched three rounds where UIA reported the Open row enabled at a
  # real rectangle, a click went there and no #32770 ever appeared. SwitchToThisWindow is the Alt+Tab
  # call; it crosses processes without SetForegroundWindow's ownership fight (sec.66). The foreground
  # title is printed before and after so a round that aimed at somebody else's window says so in the
  # log instead of reading as "this row does nothing". The raise is what the pixel modes do
  # (sec.28/sec.54); it is NOT what fixes the rounds where nothing lands -- sec.62 watched -Mode
  # openpicker succeed with the raise in place and then fail all three tries with it still in place.
  # Whether synthesised input lands at all is per-round (sec.41), which is why every attempt prints the
  # row it aimed at and why a caller retries instead of trusting one pass.
  $win = $null
  for ($k = 1; $k -le 10 -and -not $win; $k++) { Start-Sleep -Milliseconds 500; $win = Window $procId }
  if (-not $win) { Say '  no window for this pid'; return $null }
  $wr = $win.Current.BoundingRectangle
  [W32]::SwitchToThisWindow([IntPtr]$win.Current.NativeWindowHandle, $true)
  Start-Sleep -Milliseconds 500
  Say ('  foreground before the raise [' + [W32]::Text([W32]::GetForegroundWindow()) + ']')
  [W32]::Click([int]($wr.X + $wr.Width / 2), [int]($wr.Y + 12))
  Start-Sleep -Milliseconds 800
  Say ('  foreground after the raise  [' + [W32]::Text([W32]::GetForegroundWindow()) + ']')
  $pk = $null
  for ($try = 1; $try -le 3 -and -not $pk; $try++) {
    TopMenu $procId @('File', (CjStr 0x6587,0x4EF6)) | Out-Null
    $row = RowAny $procId @('Open', (CjStr 0x6253,0x5F00))
    if (-not $row) { Say ('  try ' + $try + ': no Open row under File'); Start-Sleep -Milliseconds 600; continue }
    $rr = $row.Current.BoundingRectangle
    Say ('  try ' + $try + ': menu row |' + $row.Current.Name + '| enabled=' + $row.Current.IsEnabled +
         ' at ' + [int]$rr.X + ',' + [int]$rr.Y + ' ' + [int]$rr.Width + 'x' + [int]$rr.Height)
    ClickAt $row
    for ($k = 1; $k -le 8 -and -not $pk; $k++) {
      Start-Sleep -Milliseconds 500
      $pk = FindPicker $procId
    }
    if ($pk) { Say ('  native dialog up after try ' + $try + ' [' + $pk.Current.Name + ']') }
  }
  return $pk
}

function SetPickerFieldRaw($pk, [int]$wantId, [string]$text) {
  # WM_SETTEXT of this exact string into the picker child with control id $wantId, with the spelling
  # left ALONE. TypePathIntoPicker normalises slashes on purpose, because a caller's bash-shaped path
  # should not decide whether a round can work; this is the opposite case -- the spelling is the thing
  # under test. Nothing can be read back (these fields answer empty to WM_GETTEXT, sec.44), so the
  # caller has to judge from its own receipt, and every child is dumped first because these ids have
  # been guessed wrong here more than once (sec.90: 1148 is the File name box, 1001 is the address bar).
  $kids = PickerChildren $pk
  foreach ($h in $kids) {
    Say ('    id=' + [W32]::GetDlgCtrlID($h) + ' class=' + [W32]::Cls($h) + ' text=[' + [W32]::Text($h) + ']')
  }
  $targets = @($kids | Where-Object { [W32]::GetDlgCtrlID($_) -eq $wantId })
  if ($targets.Count -eq 0) { Say ('  no child with control id ' + $wantId); return $false }
  foreach ($h in $targets) {
    Say ('  WM_SETTEXT raw [' + $text + '] (' + $text.Length + ' chars) into id=' + $wantId + ' class=' + [W32]::Cls($h))
    [W32]::SendMessageW($h, 0x000C, [IntPtr]::Zero, $text) | Out-Null
  }
  return $true
}

function PressPickerAccept($pk) {
  # BM_CLICK to the button whose control id is 1. Posting the click is not acceptance (sec.88 made
  # that a rule after PickFolder returned true on a box that was still open and complaining); this
  # only reports whether the button exists.
  $kids = New-Object System.Collections.Generic.List[IntPtr]
  $cb = [W32+EnumProc] { param($h, $l) $kids.Add($h); return $true }
  [W32]::EnumChildWindows([IntPtr]$pk.Current.NativeWindowHandle, $cb, [IntPtr]::Zero) | Out-Null
  $ok = $null
  foreach ($h in $kids) {
    if ([W32]::Cls($h) -eq 'Button' -and [W32]::GetDlgCtrlID($h) -eq 1) { $ok = $h }
  }
  if (-not $ok) { Say '  the picker has no button with control id 1'; return $false }
  [W32]::PostMessageW($ok, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
  return $true
}

function Open-PackagedDocument($theApp, [string]$rowName) {
  # Gets a document into an instance launched from AppsFolder, the only way a packaged copy can be
  # started from here. Each step prints because a round that fails quietly looks exactly like a menu
  # with nothing in it (sec.58's rule, applied to this harness).
  #   raise -- SwitchToThisWindow first; without it an Explorer window holding the foreground eats
  #            the synthesised clicks. sec.84 measured three rounds where UIA reported the Open row
  #            enabled at a real rectangle, a click went to it, and no dialog ever appeared; with
  #            this call the picker came up on the second try.
  #   choose -- click the row that names the file, then press the dialog's own Open button with
  #            BM_CLICK on control id 1, the one input the OPEN dialog honours (sec.29; the SAVE
  #            dialog is the one that ignores it, sec.44).
  $win = $null
  for ($k = 1; $k -le 10 -and -not $win; $k++) {
    Start-Sleep -Milliseconds 500
    $win = Window $theApp.Id
  }
  if (-not $win) { Say '  no window to drive'; return $false }
  $hwnd = [IntPtr]$win.Current.NativeWindowHandle
  [W32]::SwitchToThisWindow($hwnd, $true)
  Start-Sleep -Milliseconds 500
  $wr = $win.Current.BoundingRectangle
  [W32]::Click([int]($wr.X + $wr.Width / 2), [int]($wr.Y + 12))
  Start-Sleep -Milliseconds 800
  Say ('  foreground after the raise [' + [W32]::Text([W32]::GetForegroundWindow()) + ']')

  $pk = $null
  for ($try = 1; $try -le 3 -and -not $pk; $try++) {
    TopMenu $theApp.Id @('File', (CjStr 0x6587, 0x4EF6)) | Out-Null
    $row = RowAny $theApp.Id @('Open', (CjStr 0x6253, 0x5F00))
    if (-not $row) { Say ('  try ' + $try + ': no Open row under File'); continue }
    Say ('  try ' + $try + ': row |' + $row.Current.Name + '| enabled=' + $row.Current.IsEnabled)
    ClickAt $row
    for ($k = 1; $k -le 10 -and -not $pk; $k++) {
      Start-Sleep -Milliseconds 500
      foreach ($w in @($AE::RootElement.FindAll($TS::Children, (New-Object $PC($AE::ProcessIdProperty, $theApp.Id))))) {
        foreach ($e in (@($w) + @(FindType $w $CT::Window))) {
          if ($e.Current.ClassName -eq '#32770') { $pk = $e; break }
        }
        if ($pk) { break }
      }
    }
    if ($pk) { Say ('  picker up after try ' + $try + ' [' + $pk.Current.Name + ']') }
  }
  if (-not $pk) { Say '  the open dialog never came up in three tries'; return $false }

  $base = [System.IO.Path]::GetFileName($rowName)
  $target = $null
  foreach ($e in @(FindType $pk $CT::ListItem)) {
    Say ('  picker row |' + $e.Current.Name + '|')
    if ($e.Current.Name -like ('*' + $base + '*') -and -not $target) { $target = $e }
  }

  $kids = PickerChildren $pk

  if (-not $target) {
    Say ('  no row naming [' + $base + '] in the folder the picker opened at; controls in the box:')
    if ($rowName -notmatch '[\\:]') {
      Say '  -OpenViaPicker gave a bare name, so the row click is the only route'
      return $false
    }
    if (-not (TypePathIntoPicker $pk $rowName)) { return $false }
  } else {
    ClickAt $target
    Start-Sleep -Milliseconds 600
  }
  $go = $null
  foreach ($h in $kids) {
    if ([W32]::Cls($h) -eq 'Button' -and [W32]::GetDlgCtrlID($h) -eq 1) { $go = $h }
  }
  if (-not $go) { Say '  no Open button with control id 1 in the picker'; return $false }
  [W32]::PostMessageW($go, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null

  for ($k = 1; $k -le 30; $k++) {
    Start-Sleep -Milliseconds 500
    $w2 = Window $theApp.Id
    if ($w2 -and $w2.Current.Name -like ('*' + $base + '*')) {
      Say ('  document open in the packaged instance [' + $w2.Current.Name + ']')
      return $true
    }
  }
  Say '  the picker was accepted but no window naming the file appeared'
  return $false
}

function MenuBarItems($win) {
  # Re-enumerated for every reading rather than held: the top-level items are the ones whose
  # ExpandCollapseState answers "did this Alt+letter open a menu", and holding their elements
  # across a menu open/close is the kind of stale handle sec.45 warned about.
  foreach ($b in @(FindType $win $CT::MenuBar)) {
    return @($b.FindAll($TS::Children, $ANY))
  }
  return @()
}
function PageStatusCount($procId) {
  # The page count the main window states itself, in the status bar's "1 / 40" label. The
  # properties dialog is compared against THIS and not against a number written here: a
  # hard-coded 40 would pass on the 40-page fixture whether or not the dialog was read at all.
  foreach ($sb in @(FindType (Window $procId) $CT::StatusBar)) {
    foreach ($t in @(FindType $sb $CT::Text)) {
      if ($t.Current.Name -match '^[0-9]+ / ([0-9]+)$') { return [int]$Matches[1] }
    }
  }
  return -1
}
function FindClass($procId, [string]$className) {
  # Two layers, both required: sec.85 measured the delete box inside the main window's subtree
  # while sec.87 measured the merge box only among the process's own top-level windows. The
  # class name is the widget's C++ class, which no translation can change; the title is ours.
  $main = Window $procId
  if ($main) {
    foreach ($e in @(FindType $main $CT::Window)) {
      if ($e.Current.ClassName -eq $className) { return $e }
    }
  }
  foreach ($w in @($AE::RootElement.FindAll($TS::Children, (New-Object $PC($AE::ProcessIdProperty, $procId))))) {
    if ($w.Current.ClassName -eq $className) { return $w }
  }
  # Last resort: a Qt dialog that is not typed Window in the subtree has been seen here before
  # (the first properties round found it only by scanning every descendant). The class name is
  # still the widget's C++ class, so this cannot match a translation.
  if ($main) {
    foreach ($e in @($main.FindAll($TS::Descendants, $ANY))) {
      if ($e.Current.ClassName -eq $className) { return $e }
    }
  }
  return $null
}
function WaitClass($procId, [string]$className, [int]$rounds = 20) {
  for ($k = 1; $k -le $rounds; $k++) {
    $d = FindClass $procId $className
    if ($d) { Say ('  ' + $className + ' found on round ' + $k); return $d }
    Start-Sleep -Milliseconds 400
  }
  return $null
}
function WaitClassGone($procId, [string]$className, [int]$rounds = 20) {
  for ($k = 1; $k -le $rounds; $k++) {
    if (-not (FindClass $procId $className)) { return $true }
    Start-Sleep -Milliseconds 400
  }
  return $false
}
function FindAuto($root, [string]$id) {
  # A widget's objectName is the stable name for a control whose label is translated
  # (findField / findCaseBox / findWholeBox / findCount), but Qt puts the whole object path in
  # UIA's AutomationId -- QApplication.mainWindow...DocumentTab.findBar.findField -- so an exact
  # equality condition finds nothing (measured 2026-10-22). Compare the tail segment instead.
  foreach ($e in @($root.FindAll($TS::Descendants, $ANY))) {
    $aid = $e.Current.AutomationId
    if ($aid -eq $id -or $aid -like ('*.' + $id)) { return $e }
  }
  return $null
}
function WaitName($e, [string]$want, [int]$rounds = 24) {
  # A QLabel's UIA Name follows its text, and the search that fills it runs on a worker; the
  # count therefore arrives some time after the needle is typed. Poll for the exact sentence
  # instead of sleeping and reading once -- a single read at the wrong moment looks identical
  # to a label that never changed.
  for ($k = 1; $k -le $rounds; $k++) {
    if ($e.Current.Name -eq $want) { return $true }
    Start-Sleep -Milliseconds 250
  }
  return $false
}
function TabNamed($win, [string[]]$needles) {
  # Exact names, because the sidebar's tab texts are the thing under test: a fuzzy match would
  # accept "Thumbnails (preview of changes)" when the plain tab was expected.
  foreach ($t in @(FindType $win $CT::TabItem)) {
    foreach ($n in $needles) {
      if ($t.Current.Name -eq $n) { return $t }
    }
  }
  return $null
}
function ThumbItems($win) {
  # The strip is the QListView whose widget class is ThumbnailPanel. Found by class, not by
  # the tab title: that title is translated and changes spelling once there is an edit to
  # preview, while the class name cannot change. Its rows are direct children (measured).
  foreach ($e in @(FindType $win $CT::List)) {
    if ($e.Current.ClassName -ne 'ThumbnailPanel') { continue }
    return @($e.FindAll($TS::Children, $ANY) | Where-Object {
      $_.Current.ControlType.ProgrammaticName -eq 'ControlType.ListItem' })
  }
  return @()
}
function ThumbRed($win, [string]$tag) {
  # Measure the red block inside the first thumbnail, and say where the picture came from.
  # Returns a hashtable rather than printing a result: a helper that writes a line to the
  # pipeline makes its return value an array (the mistake that turned StatusInk's count into
  # a two-element array). The caller prints the fields.
  #
  # Two subtleties, both measured:
  #  * the picture is anchored to the window FRAME, so the row's UIA rectangle has to be
  #    mapped with GetWindowRect's origin, never the client rectangle (sec.89/91);
  #  * the row's bottom carries the page-number label, which is painted RED when selected.
  #    Scanning the whole row once produced a 94x168 bbox for a block that is 94x77; the top
  #    78% holds the thumbnail itself and none of that label.
  $items = @(ThumbItems $win)
  if ($items.Count -eq 0) { return @{ ok = $false; why = 'no row in ThumbnailPanel' } }
  $item = $items[0]
  $ir = $item.Current.BoundingRectangle
  $hwnd = [IntPtr]$win.Current.NativeWindowHandle
  $gr = [W32]::Rect($hwnd)
  $co = [W32]::ClientOrigin($hwnd)
  $fw = [int]($gr.right - $gr.left)
  $fh = [int]($gr.bottom - $gr.top)
  $left = [int][math]::Floor($ir.X - $gr.left)
  $top = [int][math]::Floor($ir.Y - $gr.top)
  $width = [int][math]::Ceiling($ir.Width)
  $height = [int][math]::Ceiling($ir.Height)
  $cl = $co.x - $gr.left
  $ct = $co.y - $gr.top
  if ($width -lt 20 -or $height -lt 20) {
    return @{ ok = $false; why = ('the first row is only ' + $width + 'x' + $height) }
  }
  if ($left -lt $cl -or $top -lt $ct -or ($left + $width) -gt $fw -or ($top + $height) -gt $fh) {
    return @{ ok = $false; why = ('the row at ' + $left + ',' + $top + ' ' + $width + 'x' + $height +
              ' is not inside the window picture (' + $fw + 'x' + $fh + ', client starts ' + $cl + ',' + $ct + ')') }
  }
  $winBmp = New-Object System.Drawing.Bitmap($fw, $fh)
  $wg = [System.Drawing.Graphics]::FromImage($winBmp)
  $hdc = $wg.GetHdc()
  $painted = [W32]::PrintWindow($hwnd, $hdc, 2)
  $wg.ReleaseHdc($hdc)
  $wg.Dispose()
  $crop = $winBmp.Clone((New-Object System.Drawing.Rectangle($left, $top, $width, $height)),
                        $winBmp.PixelFormat)
  $leaf = [System.IO.Path]::GetFileNameWithoutExtension($Doc)
  $shot = Join-Path $scratch ('strippreview-' + $tag + '-' + $leaf + '.png')
  $crop.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
  $scanBottom = [int]($height * 0.78)
  $red = 0; $x0 = $width; $y0 = $scanBottom; $x1 = -1; $y1 = -1
  for ($y = 0; $y -lt $scanBottom; $y++) {
    for ($x = 0; $x -lt $width; $x++) {
      $c = $crop.GetPixel($x, $y)
      if ($c.R -gt 150 -and $c.G -lt 120 -and $c.B -lt 120) {
        $red++
        if ($x -lt $x0) { $x0 = $x }
        if ($x -gt $x1) { $x1 = $x }
        if ($y -lt $y0) { $y0 = $y }
        if ($y -gt $y1) { $y1 = $y }
      }
    }
  }
  $crop.Dispose(); $winBmp.Dispose()
  $bw = 0; $bh = 0
  if ($red -gt 0) { $bw = $x1 - $x0 + 1; $bh = $y1 - $y0 + 1 }
  return @{ ok = $true; printed = $painted; red = $red; bw = $bw; bh = $bh;
            row = ($width.ToString() + 'x' + $height.ToString()); scanned = $scanBottom; shot = $shot }
}
function ForegroundIs($win) {
  if (-not $win) { return $false }
  $fg = [W32]::GetForegroundWindow()
  $hwnd = [IntPtr]$win.Current.NativeWindowHandle
  return $fg.Equals($hwnd)
}
function TakeForeground($procId) {
  # Keyboard input goes wherever the foreground is, so a mode that sends a key must own it.
  # Measured on this machine: SwitchToThisWindow, SetForegroundWindow, BringWindowToTop and a
  # click on our own title bar all left the foreground on the console/IDE; expanding a top-level
  # menu through UIA does activate the app. So: expand the first menu, close it with Esc, and
  # only believe the reading afterwards. Esc is sent ONLY if rows really appeared -- otherwise
  # the key would go to whichever window does own the screen, which is what this avoids.
  $win = Window $procId
  if (-not $win) { return $null }
  $hwnd = [IntPtr]$win.Current.NativeWindowHandle
  Say ('  foreground before [' + [W32]::Text([W32]::GetForegroundWindow()) + ']')
  if (-not (ForegroundIs $win)) {
    $before = @(MenuItems $procId).Count
    $bar = $null
    foreach ($b in @(FindType $win $CT::MenuBar)) { $bar = $b; break }
    $first = $null
    if ($bar) { foreach ($b in @($bar.FindAll($TS::Children, $ANY))) { $first = $b; break } }
    if ($first) {
      $xp = Pat $first 'ExpandCollapse'
      if ($xp) { try { $xp.Expand() } catch { Say ('  Expand threw: ' + $_.Exception.Message) } }
    }
    Start-Sleep -Milliseconds 500
    if (@(MenuItems $procId).Count -gt $before) {
      [System.Windows.Forms.SendKeys]::SendWait('{ESC}')
      Start-Sleep -Milliseconds 500
    } else {
      Say '  the menu never showed rows, so no Esc was sent'
    }
  }
  $fg = [W32]::GetForegroundWindow()
  Say ('  foreground after  [' + [W32]::Text($fg) + ']')
  if (-not $fg.Equals($hwnd)) { return $null }
  return (Window $procId)
}

function LongestText($root) {
  # The summary of an edit dialog is ONE QLabel to which every sentence the editor's answers add is
  # appended, so the longest Text element is the one carrying them (deletesummary reads it the same
  # way). The caller prints the count before filtering by length: "no summary printed" has to stay
  # distinguishable from "no summary found".
  $longest = ''
  foreach ($t in @(FindType $root $CT::Text)) {
    if ($t.Current.Name.Length -gt $longest.Length) { $longest = $t.Current.Name }
  }
  return $longest
}

function CandidateWindows($procId) {
  # Every window this process owns, both layers: the windows inside the main window's subtree and the
  # process's own top-level ones. sec.85 measured the delete box in the first, sec.87 measured the
  # merge box only in the second, so a lookup that tries one and prints nothing explains nothing.
  $out = @()
  $main = Window $procId
  if ($main) { $out += @(FindType $main $CT::Window) }
  foreach ($w in @($AE::RootElement.FindAll($TS::Children, (New-Object $PC($AE::ProcessIdProperty, $procId))))) {
    $out += $w
  }
  return $out
}

function PrintCandidates([int]$procId, [string]$why) {
  Say ('  ' + $why)
  foreach ($c in @(CandidateWindows $procId)) { Say ('    [' + $c.Current.Name + '] class=' + $c.Current.ClassName) }
}

function OpenDocumentDialog($procId, [string[]]$rowNeedles, [string]$className) {
  # Press a Document-menu row and hand back the dialog it opens, matched by CLASS. By class and not
  # by title because the title is one of our own translatable strings (sec.45) -- the row list
  # carries both spellings, the window test carries a C++ name no catalogue can reach. The raise is
  # deletesummary's route (sec.84): a click sent while another window owns the screen lands there
  # and the row still reports enabled=True. Candidates are printed when nothing matches, because
  # "the dialog never opened" has at least three different causes and one line cannot tell them.
  $win = Window $procId
  if (-not $win) { Bail '  no document window' }
  [W32]::SwitchToThisWindow([IntPtr]$win.Current.NativeWindowHandle, $true)
  Start-Sleep -Milliseconds 400
  $wr = $win.Current.BoundingRectangle
  [W32]::Click([int]($wr.X + $wr.Width / 2), [int]($wr.Y + 12))
  Start-Sleep -Milliseconds 700
  TopMenu $procId @('Document', (CjStr 0x6587, 0x6863)) | Out-Null
  $row = RowAny $procId $rowNeedles
  if (-not $row) { Bail ('  no Document-menu row named [' + ($rowNeedles -join ', ') + '] in either language') }
  Say ('  menu row |' + $row.Current.Name + '| enabled=' + $row.Current.IsEnabled)
  ClickAt $row
  $dlg = WaitClass $procId $className 12
  if (-not $dlg) {
    PrintCandidates $procId ('no ' + $className + '; every candidate window of the process:')
    Bail ('  the ' + $className + ' did not open (candidates printed above)')
  }
  return $dlg
}

function ClickCancel($dlg, [string]$className, [int]$procId) {
  # Leave a box the way a reader leaves a box they did not mean to press, and check that it left: a
  # posted click is not an accepted dialog (sec.88), and these boxes are window-modal -- one still
  # open would eat the next menu press and the second half of a mode would quietly be reading the
  # first half's window.
  $cancel = MatchOne $dlg $CT::Button @('Cancel', (CjStr 0x53D6, 0x6D88))
  if (-not $cancel) {
    foreach ($b in @(FindType $dlg $CT::Button)) { Say ('    button |' + $b.Current.Name + '|') }
    Bail ('  no Cancel button in [' + $dlg.Current.Name + ']')
  }
  Say ('  pressing |' + $cancel.Current.Name + '| to leave the box')
  ClickAt $cancel
  if (-not (WaitClassGone $procId $className 12)) {
    Bail ('  the ' + $className + ' stayed open after Cancel')
  }
}

function RangeText($edit, [string]$text) {
  # Type a page range into one of our own Qt fields through its UIA Value pattern -- the route
  # findwhole has used since sec.109 on findField. The dialog recomputes its summary on textChanged,
  # so the write is the whole interaction; nothing is pressed afterwards.
  $vp = Pat $edit 'Value'
  if (-not $vp) { Bail '  the range field offers no Value pattern' }
  $vp.SetValue($text)
  Start-Sleep -Milliseconds 700
}

function FieldOf($dlg) {
  # The range field of the delete box: this dialog has exactly one Edit (the line beside the
  # "custom pages" radio). Printed rather than assumed, because a dialog that grows a second field
  # must not have this mode typing into whichever one UIA happens to list first.
  $edits = @(FindType $dlg $CT::Edit)
  Say ('  ' + $edits.Count + ' Edit element(s) in [' + $dlg.Current.Name + ']:')
  foreach ($e in $edits) {
    Say ('    |' + $e.Current.Name + '| id=[' + $e.Current.AutomationId + ']')
  }
  if ($edits.Count -ne 1) { Bail ('  expected one Edit, found ' + $edits.Count) }
  return $edits[0]
}

function ReplyCount([string]$summary) {
  # The number the orphan sentence names, read out of the sentence rather than assumed from the
  # document: %n is filled by Qt's plural machinery, and the catalogue side of that is pinned by an
  # i18n_smoke probe -- the screen is the other half (sec.93). -1 means no sentence was there at all.
  if ($summary -match ((CjStr 0x8FD9) + ' ([0-9]+) ' + (CjStr 0x6761, 0x56DE, 0x7B54))) { return [int]$Matches[1] }
  if ($summary -match '([0-9]+) repl') { return [int]$Matches[1] }
  return -1
}

# ---- the save guard ---------------------------------------------------------------------
# Some modes press Ctrl+S, Ctrl+Shift+S, or the Save answer of the three-question box. All three
# write a PDF, and the only code path in this program that may write a user's PDF is the program's
# own Save (AGENTS.md) -- a probe has no business being the thing that does it to a real file. F8
# turned Ctrl+S into a write, which is why -Mode saverename sat unrun from sec.116 on: it pressed
# the key against whatever -Doc named, and -Doc's default is somebody's real document.
#
# So a save mode never opens the file it was handed:
#   * the ORIGINAL's sha256 is taken before the app starts, and again when the run ends -- and on
#     every Bail on the way there, because a mode that gives up halfway is exactly the mode that
#     might have written something;
#   * the file is copied into %TEMP%\xyzpdf-gui-probe\save-<random>\ under a name made up here, and
#     THAT path replaces $Doc, so the launch line cannot name the original;
#   * the copy's own hash is read by the modes, which is what makes 'the save wrote where it says
#     it wrote' and 'the save did not happen' two different readings on disk instead of one line of
#     status text.
# A path under tests\fixtures may be read and copied -- it still is not opened.
$script:saveModes = @('saveinplace', 'closeprompt', 'saveas', 'saverename')
$script:guardArmed = $false
$script:guardBroken = $false
$script:guardOrig = ''
$script:guardOrigHash = ''
$script:guardDir = ''
$script:guardCopy = ''
$script:guardCopyLeaf = ''

function Guard-Arm([string]$original) {
  # Returns the path the app is allowed to open. The name is random on purpose: every later 'did
  # this really happen' read is then about a file that could not have been in the title bar before
  # the launch, which is the vacuous-receipt shape this suite keeps tripping on (sec.77, sec.97).
  $full = (Resolve-Path -LiteralPath $original).Path
  $script:guardOrig = $full
  $script:guardOrigHash = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash
  $rand = [guid]::NewGuid().ToString('N').Substring(0, 8)
  $script:guardDir = Join-Path $scratch ('save-' + $rand)
  if (-not (Test-Path $script:guardDir)) { New-Item -ItemType Directory -Path $script:guardDir | Out-Null }
  # An ASCII, space-free leaf. The path goes out as a bare argument to Start-Process (a space would
  # split it into two arguments and the app would open nothing) and later into a shell field, so it
  # is sanitised here rather than inherited from whatever -Doc happened to be spelled as.
  $safe = ([System.IO.Path]::GetFileName($full) -replace '[^A-Za-z0-9._-]', '-')
  $script:guardCopyLeaf = 'probe-' + $rand + '-' + $safe
  $script:guardCopy = Join-Path $script:guardDir $script:guardCopyLeaf
  Copy-Item -LiteralPath $full -Destination $script:guardCopy -Force
  $script:guardArmed = $true
  Say ('  GUARD: original [' + $full + '] sha256=' + $script:guardOrigHash)
  Say ('  GUARD: the app is handed the copy [' + $script:guardCopy + ']')
  Say ('  GUARD: copy sha256=' + (Copy-Hash) + '  (must equal the original before any Save)')
  return $script:guardCopy
}

function Copy-Hash() {
  if (-not $script:guardCopy) { return '' }
  if (-not (Test-Path -LiteralPath $script:guardCopy)) { return '(the copy is gone)' }
  return (Get-FileHash -LiteralPath $script:guardCopy -Algorithm SHA256).Hash
}

function Guard-Check([string]$tag) {
  # The one line that says whether this run was allowed to touch anything it should not. Returns
  # false when the original moved; the caller decides whether that is a Bail or the final exit code.
  if (-not $script:guardArmed) { return $true }
  $now = ''
  try { $now = (Get-FileHash -LiteralPath $script:guardOrig -Algorithm SHA256).Hash } catch { $now = '(unreadable)' }
  if ($now -eq $script:guardOrigHash) {
    Say ('  GUARD ' + $tag + ': the original is byte-for-byte what it was (' + $now + ')')
    return $true
  }
  $script:guardBroken = $true
  Say ('  GUARD ' + $tag + ': THE ORIGINAL MOVED  before=' + $script:guardOrigHash + '  after=' + $now)
  Say ('           [' + $script:guardOrig + ']')
  return $false
}

if ($script:saveModes -contains $Mode) {
  # Armed before the launch, because the launch itself is what must not name the original.
  $Doc = Guard-Arm $Doc
  if ($Installed) {
    # A packaged instance opens its own document through the picker, so the only way it can reach a
    # file is the row or path the caller names. Under a save mode that has to be the copy made above.
    if (-not $OpenViaPicker) {
      Bail ('  a packaged save run must hand the picker the copy this guard made: -OpenViaPicker ' + $script:guardCopy)
    }
    if ([System.IO.Path]::GetFileName($OpenViaPicker) -ne $script:guardCopyLeaf) {
      Bail ('  -OpenViaPicker names [' + [System.IO.Path]::GetFileName($OpenViaPicker) +
            '] but a save mode may only open the copy [' + $script:guardCopyLeaf + ']')
    }
  }
}

if (-not $Installed -and -not (Test-Path $Doc)) { Bail ("no document at $Doc -- pass -Doc <path to a pdf>") }
# ---- the save modes' shared routes ------------------------------------------------------
# Everything a save mode needs, in one place: two of the three modes make the same edit and read the
# same window, and a second copy of a route is how one of them ends up working in only one language
# (sec.87's lesson, applied to this harness rather than to the product).
function Has-Star([int]$procId) {
  # DocumentTab::title() appends ' *' to the file name while the edit list is not empty and
  # MainWindow::updateWindowTitle() puts that title into the caption. It is compared as part of the
  # copy's own made-up name, so a caption that never carried the copy cannot pass this.
  $w = Window $procId
  if (-not $w) { return $false }
  return $w.Current.Name.Contains($script:guardCopyLeaf + ' *')
}
function Wait-Star([int]$procId, [bool]$want, [int]$rounds = 25) {
  # Polling for a star that arrives via a repaint rather than sleeping once and reading: the same
  # reason WaitName polls (sec.109). NOTE a vanished window answers false, so a caller that is
  # waiting for the WINDOW to go away asks Wait-WindowGone, not this.
  for ($k = 1; $k -le $rounds; $k++) {
    if ((Has-Star $procId) -eq $want) { return $true }
    Start-Sleep -Milliseconds 300
  }
  return $false
}
function Rotate-CurrentPage([int]$procId) {
  # Document / Rotate Pages, the 'Current page' radio, then the accept -- the route strippreview has
  # used since sec.109, wanted here because an edit that shows up in the saved BYTES is what makes
  # 'the save happened' a disk reading and not one line of status text.
  TopMenu $procId @('Document', (CjStr 0x6587, 0x6863)) | Out-Null
  $row = RowAny $procId @('Rotate Pages', (CjStr 0x65CB, 0x8F6C, 0x9875, 0x9762))
  if (-not $row) { Bail '  no Rotate Pages row under Document in either language' }
  Say ('  menu row |' + $row.Current.Name + '| enabled=' + $row.Current.IsEnabled)
  ClickAt $row
  $dlg = WaitClass $procId 'RotatePagesDialog' 12
  if (-not $dlg) { Bail '  the rotate dialog did not open' }
  $cur = MatchOne $dlg $CT::RadioButton @('Current page', (CjStr 0x5F53, 0x524D, 0x9875))
  if (-not $cur) { Bail '  no Current page radio in either language' }
  Say ('  picking |' + $cur.Current.Name + '|')
  ClickAt $cur
  Start-Sleep -Milliseconds 600
  $go = MatchOne $dlg $CT::Button @('Rotate these pages', (CjStr 0x65CB, 0x8F6C, 0x8FD9, 0x4E9B, 0x9875))
  if (-not ($go -and $go.Current.IsEnabled)) { Bail '  the rotate accept is not lit after picking the current page' }
  Say ('  recording: |' + $go.Current.Name + '|')
  ClickAt $go
  if (-not (WaitClassGone $procId 'RotatePagesDialog' 20)) { Bail '  the rotate dialog stayed open after the press' }
  Start-Sleep -Milliseconds 600
}
function Close-Window([int]$procId) {
  # WM_CLOSE to our own main window. That message is exactly what the caption's X produces, and
  # MainWindow::closeEvent is the guard the three-answer box lives behind. A synthesised click on the
  # X would depend on the theme's button geometry; the message does not, and it can only land on a
  # window this probe launched.
  $w = Window $procId
  if (-not $w) { return $false }
  [W32]::PostMessageW([IntPtr]$w.Current.NativeWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
  return $true
}
function Wait-WindowGone([int]$procId, [int]$rounds = 30) {
  for ($k = 1; $k -le $rounds; $k++) {
    if (-not (Window $procId)) { return $true }
    Start-Sleep -Milliseconds 400
  }
  return $false
}
function Wait-PromptBox([int]$procId, [string]$tag) {
  # The three answers are a QMessageBox (WindowDialogs::question with Save | Discard | Cancel).
  # Matched by CLASS, because the title, the body and the buttons are all translatable strings; the
  # poll also requires a label, because a window on its way in answers an empty Name and every read
  # after that is about a stale handle (sec.87).
  for ($k = 1; $k -le 15; $k++) {
    Start-Sleep -Milliseconds 400
    $b = FindClass $procId 'QMessageBox'
    if ($b -and @(FindType $b $CT::Text).Count -gt 0) {
      Say ('  ' + $tag + ': box [' + $b.Current.Name + '] class=' + $b.Current.ClassName)
      foreach ($t in @(FindType $b $CT::Text)) {
        Say ('    |' + $t.Current.Name.Replace("`r", ' ').Replace("`n", ' / ') + '|')
      }
      return $b
    }
  }
  return $null
}
function PressBoxButton($box, [string[]]$names, [string]$tag) {
  # EXACT names rather than substrings: this box offers Save, Don't Save and Cancel, and a substring
  # search for Save answers on Don't Save -- where pressing the neighbour is the one thing that
  # changes the file. Printed before filtered, as everywhere else in this suite.
  $buttons = @(FindType $box $CT::Button)
  Say ('  ' + $tag + ': ' + $buttons.Count + ' Button element(s):')
  foreach ($b in $buttons) { Say ('    |' + $b.Current.Name + '| enabled=' + $b.Current.IsEnabled) }
  foreach ($b in $buttons) {
    foreach ($n in $names) {
      if ($b.Current.Name -eq $n) {
        Say ('  ' + $tag + ': pressing |' + $n + '|')
        ClickAt $b
        return $true
      }
    }
  }
  Say ('  ' + $tag + ': nothing among them is named [' + ($names -join ', ') + ']')
  return $false
}
function Wait-Status([int]$procId, [string[]]$needles, [int]$rounds = 40) {
  # A save line is TIMED (setStatusMessage gives it 5 s), so this polls instead of sleeping once and
  # reading -- a read at the wrong moment looks exactly like a line that never came. The cursor is
  # parked first because a toolbar statusTip replaces a timed message outright (sec.114).
  ParkCursorAway
  for ($k = 1; $k -le $rounds; $k++) {
    $s = StatusName $procId
    foreach ($n in $needles) { if ($s.Contains($n)) { return $s } }
    Start-Sleep -Milliseconds 200
  }
  return ''
}

if ($Log) { $lines.Clear() }
$app = Start-ProbeApp $Language
if (-not $app) { Bail 'the application never produced a window' }
if ($Installed) {
  # The -Language value is not applied: the packaged copy never sees the scratch ini,
  # because the environment does not reach a process the shell starts. So its interface is
  # whatever its own store says, which on this machine is the system locale.
  Say ('installed window [' + (Window $app.Id).Current.Name + '] pid ' + $app.Id + ' (-Language ' + $Language + ' is NOT applied to a packaged run)')
} else {
  Say ('window [' + (Window $app.Id).Current.Name + '] pid ' + $app.Id + ' language=' + $Language)
}
if ($Installed -and $OpenViaPicker) {
  # Before the mode runs, so every mode that reads a document can be pointed at the installed copy.
  if (-not (Open-PackagedDocument $app $OpenViaPicker)) { Bail 'no document reached the packaged instance' }
}

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
    # -Menu takes either a word ("Document") or hex code points ("6587,6863") so the Chinese
    # bar can be asked for from this ASCII file. TopMenu reports which channel opened it, which
    # is the whole point of running the same mode in both languages.
    $before = @(MenuItems $app.Id).Count
    TopMenu $app.Id (Needle $Menu) | Out-Null
    $rows = @(MenuItems $app.Id)
    Say ('  ' + $rows.Count + ' menu items after opening [' + (Needle $Menu) + '] (' + $before + ' before)')
    foreach ($e in $rows) { Say ('  row |' + $e.Current.Name + '| enabled=' + $e.Current.IsEnabled) }
  }
  'exportstatus' {
    # The export's receipt line, read the way sec.37 measured it: the StatusBar element's
    # Name and a crop of the message area, sampled at the same instants.
    if (-not (Test-Path $OutDir)) { Bail "no such -OutDir: $OutDir" }
    # Only the names this program writes are cleared (<document name> <nnn>.png) -- the
    # folder is somewhere the reader may keep real pictures.
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($Doc)
    Get-ChildItem $OutDir -Filter *.png -ErrorAction SilentlyContinue |
      Where-Object { $_.Name.StartsWith($stem) } | Remove-Item -Force
    $ink0 = StatusInk $app.Id (Join-Path $scratch 'status-before.png')
    Say ('before: name=[' + (StatusName $app.Id) + '] ink=' + $ink0)
    # Same lesson as sec.87's mergedonor: the row, the title and the buttons are our own
    # translatable strings, so every lookup asks for both spellings. The Chinese one is the export
    # phrase (mainwindow.cpp:562 for the row, imageexportdialog.cpp:25 for the title).
    $exportNeedles = @('Export as Images', (CjStr 0x5BFC, 0x51FA, 0x4E3A, 0x56FE, 0x7247))
    TopMenu $app.Id @('File', (CjStr 0x6587,0x4EF6)) | Out-Null
    $row = RowAny $app.Id $exportNeedles
    if (-not $row) { Bail 'no export row in either language' }
    ClickAt $row
    Start-Sleep -Milliseconds 1600
    $dlg = DialogAny $app.Id $exportNeedles -Show
    if (-not $dlg) { Bail 'the export dialog did not open' }
    $radio = Elem $dlg $CT::RadioButton 'Current page'
    if (-not $radio) { $radio = Elem $dlg $CT::RadioButton (CjStr 0x5F53,0x524D) }
    if ($radio) { ClickAt $radio; Start-Sleep -Milliseconds 400; Say ('  picked |' + $radio.Current.Name + '|') }
    $browse = Elem $dlg $CT::Button 'Browse'
    if (-not $browse) { $browse = Elem $dlg $CT::Button (CjStr 0x6D4F,0x89C8) }
    ClickAt $browse
    if (-not (PickFolder $app.Id $OutDir)) { Bail 'could not choose the -OutDir folder in the export dialog' }
    $go = MatchOne (DialogAny $app.Id $exportNeedles) $CT::Button @('Export', (CjStr 0x5BFC, 0x51FA))
    if (-not ($go -and $go.Current.IsEnabled)) { Bail '  Export is still greyed' }
    # Raise first: the native folder picker that just closed holds the foreground, and sec.84 measured
    # that a synthesised click sent while somebody else owns the screen lands on them -- which is what
    # this mode did on its first Chinese run: it pressed accept, sampled fourteen times, wrote nothing,
    # and still printed DONE.
    [W32]::SwitchToThisWindow([IntPtr]$dlg.Current.NativeWindowHandle, $true)
    Start-Sleep -Milliseconds 400
    ClickAt $go
    # What the dialog did after accept, printed once. The mode had no line for this and it mattered:
    # two runs in a row wrote no PNG while the loop cheerfully sampled 0,0,0 -- and the only way to
    # tell "the export is slow" from "a refusal box is sitting in front of us" is to look at which
    # windows the process has at that moment.
    Start-Sleep -Milliseconds 900
    $after = @()
    $mainW = Window $app.Id
    if ($mainW) { $after += @(FindType $mainW $CT::Window) }
    $after += @($AE::RootElement.FindAll($TS::Children, (New-Object $PC($AE::ProcessIdProperty, $app.Id))))
    Say ('  windows after pressing accept: ' + @($after | ForEach-Object { $_.Current.ClassName } | Sort-Object -Unique).Count + ' class(es)')
    foreach ($w in $after) {
      $txt = ''
      foreach ($t in @(FindType $w $CT::Text)) { if ($t.Current.Name.Length -gt $txt.Length) { $txt = $t.Current.Name } }
      Say ('    [' + $w.Current.Name + '] class=' + $w.Current.ClassName + ' longest text |' +
           $txt.Replace("`r", ' ').Replace("`n", ' / ') + '|')
    }
    $shot = Join-Path $scratch 'status-message.png'
    $seen = 0
    for ($k = 1; $k -le 14; $k++) {
      Start-Sleep -Milliseconds 700
      $written = @(Get-ChildItem $OutDir -Filter *.png).Count
      if ($written -gt $seen) { $seen = $written }
      $name = StatusName $app.Id
      $ink = StatusInk $app.Id ''
      if ($name -and -not (Test-Path $shot)) { $ink = StatusInk $app.Id $shot }
      Say ('  t=' + [math]::Round($k * 0.7, 1) + 's files=' + $written + ' ink=' + $ink `
           + ' name=[' + $name + '] at ' + $script:lastInkRect)
      if ($k -ge 11 -and $ink -lt $ink0) { break }
    }
    Say ('  crops kept at ' + (Join-Path $scratch 'status-before.png') + ' and ' + $shot)
    # The verdict the run used to lack: the whole point of this mode is the receipt at the end of an
    # export, and a receipt that never arrived is not a reading to print a table about.
    if ($seen -lt 1) { Bail 'no PNG appeared in -OutDir, so the export never happened -- the samples above are a dead round, not a measurement' }
    if (-not (Test-Path $shot)) { Bail 'the status message was never painted in the strip, so the crop that judges its words does not exist' }
  }
  'mergedonor' {
    # The merge box is the one edit whose undo row names a FILE, and the only way to reach
    # it is through the native open dialog -- which is why this row, unlike the other five,
    # had never been read on screen. Two donors, so both numerus branches come out of one
    # run: the row after the second merge is the newest operation.
    if (-not $LastDir) { Bail 'mergedonor needs -LastDir (the folder holding -Donor)' }
    # The merge row, the dialog title and the accept button are all our own translatable strings, so
    # each is asked for in both spellings. Before sec.87 the two lookups above were English-only,
    # which meant this mode could not run on the Chinese interface at all -- and it would have failed
    # saying "no Merge Files In row", a sentence about the harness, not about the product.
    $mergeNeedles = @('Merge Files In', (CjStr 0x5408, 0x5E76, 0x6587, 0x4EF6, 0x8FDB, 0x6765))
    $undo = {
      param([string]$tag, [bool]$expect = $false)
      # Up to three tries, because whether the popup renders at all is per-round (sec.41, sec.87) --
      # and when the caller expects a row, say so: an English installed run of this mode printed
      # NOTHING for the second donor's undo row and still exited 0, which is sec.88's shape exactly.
      $found = 0
      for ($try = 1; $try -le 3 -and -not $found; $try++) {
        TopMenu $app.Id @('Document', (CjStr 0x6587, 0x6863)) | Out-Null
        foreach ($e in @($AE::RootElement.FindAll($TS::Descendants, (New-Object $PC($AE::ProcessIdProperty, $app.Id))))) {
          if ($e.Current.ControlType.ProgrammaticName -eq 'ControlType.MenuItem' `
              -and ($e.Current.Name -like 'Undo*' -or $e.Current.Name -like ((CjStr 0x64A4, 0x9500) + '*'))) {
            $found++
            Say ('  ' + $tag + 'row |' + $e.Current.Name + '|')
          }
        }
        if (-not $found) { Say ('  ' + $tag + 'try ' + $try + ': no Undo row yet'); Start-Sleep -Milliseconds 700 }
      }
      if ($expect -and -not $found) {
        Bail ('  ' + $tag + 'no Undo row after three tries -- a merge that left nothing to undo, or a ' +
              'menu this harness failed to open; either way a blank is not a verdict')
      }
    }
    & $undo 'before: '
    # The second donor is a name, not a path, so a caller who handed -Donor a path has to get the same
    # directory for it: otherwise the first merge runs by typed path (sec.90) and the second dies on a
    # bare name that only happens to be where the picker opened.
    $secondDonor = 'donor2.pdf'
    if ($Donor -match '[\\:]') {
      $secondDonor = Join-Path ([System.IO.Path]::GetDirectoryName($Donor)) 'donor2.pdf'
    }
    foreach ($donor in @($Donor, $secondDonor)) {
      # Three tries, because whether synthesised input lands at all is per-round (sec.41, and sec.87
      # watched exactly this on a Chinese run: the first TopMenu produced rows, the next one did not,
      # and the mode bailed saying there was no merge row -- a sentence about the harness). The label
      # itself is the merge phrase with an accelerator and an ellipsis (mainwindow.cpp:877), which
      $row = $null
      for ($try = 1; $try -le 3 -and -not $row; $try++) {
        TopMenu $app.Id @('Document', (CjStr 0x6587, 0x6863)) | Out-Null
        $row = RowAny $app.Id $mergeNeedles
        if (-not $row) { Say ('  try ' + $try + ': no merge row yet'); Start-Sleep -Milliseconds 700 }
      }
      if (-not $row) { Bail 'no merge row after three tries of opening the Document menu' }
      ClickAt $row
      Start-Sleep -Milliseconds 1600
      $dlg = DialogAny $app.Id $mergeNeedles -Show
      if (-not $dlg) {
        Say '  merge dialog not found by title; windows in the subtree:'
        foreach ($w in @(FindType (Window $app.Id) $CT::Window)) { Say ('    |' + $w.Current.Name + '|') }
        Bail 'merge dialog not found by title; the subtree windows are printed above'
      }
      $add = Elem $dlg $CT::Button 'Add Files'
      if (-not $add) { $add = Elem $dlg $CT::Button (CjStr 0x6DFB,0x52A0,0x6587,0x4EF6) }
      ClickAt $add
      if (-not (PickFile $app.Id $donor)) { Bail 'the donor file was not picked in the open dialog' }
      $dlg2 = DialogAny $app.Id $mergeNeedles
      foreach ($e in @(FindType $dlg2 $CT::DataItem)) { Say ('  table row |' + $e.Current.Name + '|') }
      foreach ($e in @(FindType $dlg2 $CT::Text)) {
        if ($e.Current.Name.Length -gt 12) { Say ('  text |' + $e.Current.Name.Replace("`n", ' / ') + '|') }
      }
      $go = Elem $dlg2 $CT::Button 'Merge these files'
      if (-not $go) { $go = Elem $dlg2 $CT::Button (CjStr 0x5408,0x5E76) }
      if (-not ($go -and $go.Current.IsEnabled)) { Bail '  accept still greyed' }
      ClickAt $go
      Start-Sleep -Milliseconds 1200
      & $undo ($donor + ': ') $true
    }
    # What the status bar says after two merges is *nothing*, and that is the product's rule rather
    # than a message that failed to appear: the file owning every pending edit (documenttab.cpp) has
    # no status-message call in it at all, while the announcements all live in mainwindow.cpp and are
    # for things that finished somewhere -- images exported (:1414), a new file written (:1440), Word
    # (:1761), windows merged (:2007). A merge only records a change, so its receipts are the dirty
    # tab and the undo row, both read above. Printed as an expectation with a check behind it,
    # because a bare "status: []" next to a line that looks like a measurement reads like a failure
    # (it is what made sec.87 ask why this was empty, three rounds and two builds ago).
    $status = StatusName $app.Id
    $announces = @('merge', 'Merge', (CjStr 0x5408, 0x5E76))   # the merge wording in either interface
    $said = @($announces | Where-Object { $status -like ('*' + $_ + '*') })
    Say ('  status: [' + $status + ']  expected: no announcement -- a pending edit is not a completed ' +
         'write, and documenttab.cpp never calls setStatusMessage')
    if ($said.Count -gt 0) {
      Bail ('  the status bar now announces a merge ("' + $status + '"). Either pending edits started ' +
            'speaking or a merge began writing something when it was accepted -- both are product ' +
            'changes this line exists to notice')
    }
  }
  'splitsagain' {
    # The re-split aside is the other row whose English had lost its paragraph break, and it
    # only exists when a split is already recorded AND the new size differs from it -- so this
    # records one, reopens the box (which seeds its spin from the recorded size) and moves the
    # number. Two rounds, because the aside names the size being replaced: 1 gives the
    # singular form, 7 the plural one. Nothing is saved; the run ends by killing the process,
    # so a recorded split never reaches a file.
    foreach ($pair in @(@(1, 7), @(7, 3))) {
      $from = $pair[0]; $to = $pair[1]
      TopMenu $app.Id @('Document', (CjStr 0x6587,0x6863)) | Out-Null
      $row = RowAny $app.Id @('Split Files', (CjStr 0x62C6,0x5206,0x6587,0x4EF6))
      if (-not $row) { Bail '  no split row in either language' }
      ClickAt $row
      Start-Sleep -Milliseconds 1500
      $dlg = WaitDialog $app.Id @('Split', (CjStr 0x62C6,0x5206))
      if (-not $dlg) { Bail '  split dialog did not open' }
      $spin = @(@($dlg.FindAll($TS::Descendants, (New-Object $PC($AE::ControlTypeProperty, $CT::Spinner))))[0])
      $rv = Pat $spin 'RangeValue'
      if (-not $rv) { Bail '  the spin box has no RangeValuePattern' }
      $rv.SetValue($from)
      Start-Sleep -Milliseconds 600
      $go = MatchOne $dlg $CT::Button @('Split into', (CjStr 0x62C6,0x6210))
      Say ('  set size ' + $from + '; accept says |' + $go.Current.Name + '| enabled=' + $go.Current.IsEnabled)
      ClickAt $go
      Start-Sleep -Milliseconds 1200
      TopMenu $app.Id @('Document', (CjStr 0x6587,0x6863)) | Out-Null
      ClickAt (RowAny $app.Id @('Split Files', (CjStr 0x62C6,0x5206,0x6587,0x4EF6)))
      Start-Sleep -Milliseconds 1500
      $dlg2 = WaitDialog $app.Id @('Split', (CjStr 0x62C6,0x5206))
      if (-not $dlg2) { Bail '  reopened split dialog not found alive' }
      $spin2 = @(@($dlg2.FindAll($TS::Descendants, (New-Object $PC($AE::ControlTypeProperty, $CT::Spinner))))[0])
      (Pat $spin2 'RangeValue').SetValue($to)
      Start-Sleep -Milliseconds 800
      Say ('  reopened (seeded at ' + $from + '), now asking ' + $to + ':')
      # Count what is there before filtering by length: "no summary printed" has to be
      # distinguishable from "no summary found".
      $texts = @(FindType $dlg2 $CT::Text)
      if (-not $texts.Count) {
        Say ('    every descendant of [' + $dlg2.Current.Name + ']:')
        foreach ($e in @($dlg2.FindAll($TS::Descendants, $ANY))) {
          Say ('      ' + $e.Current.ControlType.ProgrammaticName + ' |' + $e.Current.Name + '|')
        }
      }
      Say ('    ' + $texts.Count + ' Text elements in the dialog, longest ' + `
           $(if ($texts.Count) { ($texts | ForEach-Object { $_.Current.Name.Length } | Measure-Object -Maximum).Maximum } else { 0 }) + ' chars')
      foreach ($e in $texts) {
        if ($e.Current.Name.Length -gt 12) {
          Say ('    text |' + $e.Current.Name.Replace("`r", ' ').Replace("`n", ' / ') + '|')
        }
      }
      ClickAt (MatchOne $dlg2 $CT::Button @('Split into', (CjStr 0x62C6,0x6210)))
      Start-Sleep -Milliseconds 1200
    }
    TopMenu $app.Id @('Document', (CjStr 0x6587,0x6863)) | Out-Null
    foreach ($e in @($AE::RootElement.FindAll($TS::Descendants, (New-Object $PC($AE::ProcessIdProperty, $app.Id))))) {
      if ($e.Current.ControlType.ProgrammaticName -eq 'ControlType.MenuItem' `
          -and ($e.Current.Name -like 'Undo*' -or $e.Current.Name -like ((CjStr 0x64A4,0x9500) + '*'))) {
        Say ('  undo row |' + $e.Current.Name + '|') }
    }
  }
  'saveinplace' {
    # What F8 made Ctrl+S: write the pending edit back into the file that is open. Neither the
    # in-place save nor the three-answer box has ever been read off a screen (sec.116 says so about
    # both), and the mode that used to press the key -- saverename -- has been unrun since, because
    # the key it pressed took the -Doc file with it. Everything below therefore happens on the copy
    # the guard above made, and the original is only ever hashed.
    #
    # The edit is a rotation through the dialog, because that changes the BYTES of the file rather
    # than only the view: 'the save happened' is then a disk reading, not a line of text. Four things
    # have to agree afterwards -- the status line says it saved and names the copy, the caption's star
    # is gone, the copy's sha256 moved, and (the guard) the original's did not.
    #
    # -Control keeps all four expectations and does not press the key. The status line is timed at 5 s
    # so it is polled, never slept-once-and-read.
    $win = TakeForeground $app.Id
    if (-not $win) { Bail '  could not take the foreground; refusing to send Ctrl+S into another window' }
    $title = (Window $app.Id).Current.Name
    Say ('  window [' + $title + ']')
    if (-not $title.Contains($script:guardCopyLeaf)) {
      Bail ('  the caption does not name the copy this probe made [' + $script:guardCopyLeaf + ']')
    }
    $copyBefore = Copy-Hash
    if ($copyBefore -ne $script:guardOrigHash) {
      Bail ('  the copy is not the original before anything was saved (' + $copyBefore + ')')
    }
    Say ('  the copy equals the original before the edit (' + $copyBefore + ')')

    Rotate-CurrentPage $app.Id
    if (-not (Wait-Star $app.Id $true)) {
      Bail ('  the caption never grew the unsaved-edits star; it reads [' + (Window $app.Id).Current.Name + ']')
    }
    Say ('  caption after the rotation [' + (Window $app.Id).Current.Name + '] -- the star is there')

    # The key only goes where the window is: the dialog that just closed had the foreground, and a
    # chord sent to whoever holds it now would be somebody else's Ctrl+S.
    if (-not (ForegroundIs (Window $app.Id))) {
      Say ('  foreground is [' + [W32]::Text([W32]::GetForegroundWindow()) + '], not ours; raising')
      if (-not (TakeForeground $app.Id)) { Bail '  could not get the foreground back after the rotate dialog' }
    }
    if ($Control) {
      Say '  CONTROL: Ctrl+S is not pressed; every expectation below stays the same'
    } else {
      [System.Windows.Forms.SendKeys]::SendWait('^s')
    }

    $savedCj = CjStr 0x5DF2, 0x4FDD, 0x5B58, 0x5230   # the source form of the save line
    $msg = Wait-Status $app.Id @($savedCj, 'Saved to')
    $sawMsg = ($msg -ne '')
    $msgNamesCopy = $msg.Contains($script:guardCopyLeaf)
    $starGone = Wait-Star $app.Id $false
    $copyAfter = Copy-Hash
    $moved = ($copyAfter -ne $copyBefore)
    Say ('  status line [' + $msg.Replace("`r", ' ').Replace("`n", ' / ') + ']')
    Say ('  a save line=' + $sawMsg + '  it names the copy=' + $msgNamesCopy +
         '  the star went=' + $starGone + '  the copy moved=' + $moved)
    Say ('  copy sha256 before [' + $copyBefore + ']')
    Say ('  copy sha256 after  [' + $copyAfter + ']')
    if (-not $sawMsg) {
      if ($Control) {
        Bail '  CONTROL: with the key unpressed no save line appeared -- the status verdict is live (CONTROL exit 1)'
      }
      Bail '  Ctrl+S left no "saved to" line in the status bar'
    }
    if (-not $msgNamesCopy) { Bail ('  the save line does not name the file that is open [' + $script:guardCopy + ']') }
    if (-not $starGone) { Bail '  the caption still carries the star, so the tab still thinks something is unwritten' }
    if (-not $moved) { Bail '  the copy on disk is what it was, so nothing was written despite the line saying it was' }
    if (-not (Guard-Check 'after the in-place save')) {
      Bail '  the in-place save wrote the file this probe was handed rather than its own copy'
    }
    Say ('  Ctrl+S wrote the copy: the line names it, the star is gone, the bytes moved, the original did not')
  }
  'saverename' {
    # Renaming in the native save box works, and the route is neither of the two obvious ones
    # (measured 2026-09-27, sec.44; it cost sec.41 a whole round of wrong conclusions):
    #   * WM_SETTEXT to the Edit with control id 1001 DOES change the name -- proven by the file
    #     that comes out, because WM_GETTEXT on that same Edit answers empty while the screen
    #     shows text, so the box cannot be read back at all.
    #   * BM_CLICK to the button with control id 1 does NOT accept this dialog, even though the
    #     same call accepts the native open dialog (sec.29) and the folder picker (sec.37/38).
    #     What accepts it is a real click at the button's own GetWindowRect centre.
    #   * The keyboard is not an option here: SendKeys into this box gets eaten by the Pinyin IME
    #     ("renamed-44.pdf" came out as composition text with a candidate strip open).
    # Acceptance is then read from our own receipts: the file on disk, the tab, the status line,
    # and the source hash, which must not move.
    $target = Join-Path $OutDir ('renamed-' + [System.DateTime]::Now.ToString('HHmmss') + '.pdf')
    # Backslashes, not forward ones: this string goes into a Win32 field read by the shell.
    if (Test-Path $target) { Remove-Item $target -Force }
    $sourceHash = (Get-FileHash -Path $Doc -Algorithm SHA256).Hash

    if ($Installed) {
      # Nothing behind the native open dialog had ever been reached on the packaged copy until
      # sec.43; an edit needs a document first, and the packaged app starts without one.
      #
      # The click is retried up to three times, the way the save row below has been since sec.45,
      # because Expand() reports the menu as open even on rounds where the following click does
      # not land (measured again 2026-09-30: two rounds printed 'opened [File] via Expand' and
      # then 'no native dialog within 8s', with nothing but the main window in the subtree). Each
      # try prints the row it aimed at and the dialog it looked for, so a miss is a reading rather
      # than a guess about routing. Success is judged by the dialog's CLASS, '#32770', never by
      # its title -- the caption is our own translatable string (sec.39).
      $picked = $false
      for ($try = 1; $try -le 3 -and -not $picked; $try++) {
        TopMenu $app.Id @('File', (CjStr 0x6587,0x4EF6)) | Out-Null
        $openRow = RowAny $app.Id @('Open', (CjStr 0x6253,0x5F00))
        if (-not $openRow) { Say ('  try ' + $try + ': no Open row under File'); Start-Sleep -Milliseconds 600; continue }
        $or = $openRow.Current.BoundingRectangle
        Say ('  try ' + $try + ': menu row |' + $openRow.Current.Name + '| enabled=' + $openRow.Current.IsEnabled +
             ' at ' + [int]$or.X + ',' + [int]$or.Y + ' ' + [int]$or.Width + 'x' + [int]$or.Height)
        ClickAt $openRow
        for ($k = 1; $k -le 8 -and -not $picked; $k++) {
          Start-Sleep -Milliseconds 500
          foreach ($w in @($AE::RootElement.FindAll($TS::Children, (New-Object $PC($AE::ProcessIdProperty, $app.Id))))) {
            foreach ($e in (@($w) + @(FindType $w $CT::Window))) {
              if ($e.Current.ClassName -eq '#32770') {
                Say ('  native dialog after try ' + $try + ' round ' + [math]::Round($k * 0.5, 1) + 's [' + $e.Current.Name + ']')
                $picked = $true
                break
              }
            }
            if ($picked) { break }
          }
        }
      }
      if (-not $picked) { Bail '  the open dialog never came up in three tries' }
      # The full path, not the bare name: the row click still wins when the folder happens to hold the
      # file (that is how this mode ran since sec.45), and when it does not -- which is the normal case
      # for a packaged instance, whose start directory is whatever its last open left behind -- the
      # typed-path fallback in PickFile is what reaches for it.
      if (-not (PickFile $app.Id $Doc)) { Bail 'the document was not picked in the open dialog' }
      Start-Sleep -Seconds 2
      Say ('  document window [' + (Window $app.Id).Current.Name + ']')
    }

    TopMenu $app.Id @('Document', (CjStr 0x6587,0x6863)) | Out-Null
    ClickAt (RowAny $app.Id @('Rotate Pages', (CjStr 0x65CB,0x8F6C,0x9875,0x9762)))
    Start-Sleep -Milliseconds 1500
    # Print what is actually there before filtering it: every guess about a dialog title or
    # a button label has been wrong at least once in this suite.
    foreach ($w in @(FindType (Window $app.Id) $CT::Window)) {
      Say ('  window in subtree [' + $w.Current.Name + '] class=' + $w.Current.ClassName)
    }
    # Ours by CLASS: a Qt dialog reports its widget class name, which no translation can change,
    # while its title is one of our own translatable strings.
    $rot = $null
    foreach ($w in @(FindType (Window $app.Id) $CT::Window)) {
      if ($w.Current.ClassName -eq 'RotatePagesDialog') { $rot = $w }
    }
    if (-not $rot) { Bail '  rotate dialog not found' }
    foreach ($b in @(FindType $rot $CT::Button)) {
      Say ('    button |' + $b.Current.Name + '| enabled=' + $b.Current.IsEnabled)
    }
    # The box opens on the typed-range choice with an empty field, so its accept is greyed by
    # design (sec.14: refuse before the press rather than guess a range). Name a page first.
    $cur = MatchOne $rot $CT::RadioButton @('Current page', (CjStr 0x5F53,0x524D,0x9875))
    if ($cur) { ClickAt $cur; Start-Sleep -Milliseconds 600; Say ('  picked |' + $cur.Current.Name + '|') }
    $ok = MatchOne $rot $CT::Button @('Rotate', (CjStr 0x65CB,0x8F6C))
    if (-not ($ok -and $ok.Current.IsEnabled)) { Bail '  rotate accept not lit' }
    Say ('  recording: |' + $ok.Current.Name + '|')
    ClickAt $ok
    Start-Sleep -Milliseconds 1200

    # The row click is retried: Expand() has been seen to report the menu as open without the
    # following click landing, so the dialog's absence is polled for and the whole attempt redone
    # rather than concluding the menu is broken.
    $sv = $null
    for ($try = 1; $try -le 3 -and -not $sv; $try++) {
      TopMenu $app.Id @('Document', (CjStr 0x6587,0x6863)) | Out-Null
      $saveRow = RowAny $app.Id @('Save Changes As', (CjStr 0x4FDD,0x5B58,0x4FEE,0x6539,0x4E3A))
      if (-not $saveRow) { Bail '  no Save Changes As row' }
      $rr = $saveRow.Current.BoundingRectangle
      Say ('  try ' + $try + ': menu row |' + $saveRow.Current.Name + '| enabled=' + $saveRow.Current.IsEnabled + ' at ' + [int]$rr.X + ',' + [int]$rr.Y + ' ' + [int]$rr.Width + 'x' + [int]$rr.Height)
      ClickAt $saveRow
      for ($k = 1; $k -le 8; $k++) {
        Start-Sleep -Milliseconds 500
      foreach ($w in @($AE::RootElement.FindAll($TS::Children, (New-Object $PC($AE::ProcessIdProperty, $app.Id))))) {
        foreach ($e in (@($w) + @(FindType $w $CT::Window))) {
          if ($e.Current.ClassName -eq '#32770') { $sv = $e }
        }
      }
        if ($sv) { break }
      }
    }
    if (-not $sv) { Bail '  no native save dialog after three tries' }
    Say ('  save dialog [' + $sv.Current.Name + ']')
    $kids = New-Object System.Collections.Generic.List[IntPtr]
    $cb = [W32+EnumProc] { param($h, $l) $kids.Add($h); return $true }
    [W32]::EnumChildWindows([IntPtr]$sv.Current.NativeWindowHandle, $cb, [IntPtr]::Zero) | Out-Null
    $go = $null
    foreach ($h in $kids) {
      $cls = [W32]::Cls($h); $id = [W32]::GetDlgCtrlID($h)
      # The text printed here is what Win32 ANSWERS, not what is drawn: on the name field it
      # is empty while the screen shows the seeded file name.
      if ($cls -eq 'Edit') { Say ('    Edit id=' + $id + ' answers [' + [W32]::Text($h) + ']') }
      if ($cls -eq 'Button' -and $id -eq 1) { $go = $h }
    }
    $nameBox = NameField $kids
    if (-not ($nameBox -and $go)) { Bail '  no file-name field / no Button id=1' }
    [W32]::SendMessageW($nameBox, 0x000C, [IntPtr]0, $target) | Out-Null
    Start-Sleep -Milliseconds 700
    $blind = [W32]::Text($nameBox)
    Say ('  wrote [' + $target + '], box answers [' + $blind + '] -- an empty answer proves nothing, see the note above')
    # A real click on the drawn button: BM_CLICK to this control id is answered by the open
    # dialog and the folder picker, but NOT by the save dialog.
    $gr = [W32]::Rect([IntPtr]$go)
    [W32]::Click([int](($gr.left + $gr.right) / 2), [int](($gr.top + $gr.bottom) / 2))
    Start-Sleep -Seconds 4
    Say ('  target exists: ' + (Test-Path $target) + ' size ' + `
         $(if (Test-Path $target) { (Get-Item $target).Length } else { 0 }))
    Say ('  source hash unchanged: ' + `
         $((Get-FileHash -Path $Doc -Algorithm SHA256).Hash -eq $sourceHash))
    Say ('  status: [' + (StatusName $app.Id) + ']')
    foreach ($t in @(FindType (Window $app.Id) $CT::TabItem)) { Say ('  tab |' + $t.Current.Name + '|') }
  }
  'langswitch' {
    # An installed copy speaks the system locale, and no setting written from outside can reach it
    # (sec.48 measured a whole packaged session that left HKCU:\Software\xyzpdf untouched). The door
    # in is the app's own Language menu, and the reason a reader can find that door at all is
    # deliberate: mainwindow.cpp:1030 gives it QStringLiteral("Language") rather than tr(), because
    # whoever needs the entry is looking at an interface they cannot read.
    #
    # Nothing here is read from the registry. This package's AppContainer storage key holds only
    # ResourcesConfig\ManifestLanguagesList -- the values themselves live in the Helium hive, which
    # no shell outside the container opens -- so the app's own settings dialog is the instrument.
    $want = Needle $To
    Say ('  asked for [' + $want + ']  installed=' + [bool]$Installed + '  keep=' + [bool]$Keep)
    # The settings dialog is read BEFORE the menu is opened, because opening the dialog needs the
    # Help menu, and a Help click while the Language popup is open would be read as the popup
    # misbehaving rather than as the route it is.
    $before = LanguageLabel $app.Id 'at start'
    Say ('  at start the app reports [' + $before + ']')
    Say ('  menu bar: |' + ((MenuBarNames $app.Id) -join '| |') + '|')
    $rows = @(LanguageRows $app.Id 'at start')
    if (-not (ClickLanguageRow $rows @(LanguageNeedles $want) 'switch')) { Bail 'the Language menu has no row for the requested language' }
    if (-not (DismissNotice $app.Id 'switch')) { Bail 'the restart notice after the language switch was not dismissed' }

    $mid = LanguageLabel $app.Id 'without a restart'
    Say ('  after the click, before any restart the app reports [' + $mid + '] ' +
         '-- the store took the value; the interface cannot change under itself by design')

    if ($Keep) { Say '  -Keep set: the language is left as switched'; exit 0 }

    $app2 = Restart-ProbeApp $Language
    if (-not $app2) { Bail '  the relaunch produced no window' }
    Say ('  relaunched: window [' + (Window $app2.Id).Current.Name + ']')
    Say ('  menu bar now: |' + ((MenuBarNames $app2.Id) -join '| |') + '|')
    Say ('  status line now: [' + (StatusName $app2.Id) + ']')
    Say ('  the app reports [' + (LanguageLabel $app2.Id 'after a restart') + ']')
    $rows2 = @(LanguageRows $app2.Id 'after a restart')

    # Put it back the way it was, then read the bar and the check once more. A restore that did not
    # work would be a finding of its own -- a one-way switch is the last thing a user of either
    # language wants -- and the reopen is also what shows the checkmark MOVES rather than sitting on
    # whichever row the menu was built from.
    # Whichever interface the restore is clicked in, the behaviour row may be spelled either way.
    $back = @(LanguageNeedles $before)
    if (-not (ClickLanguageRow $rows2 $back 'restore')) {
      Bail 'RESTORE FAILED: the installed copy is left in the language it reported above'
    }
    if (-not (DismissNotice $app2.Id 'restore')) { Bail 'the restart notice after restoring the language was not dismissed' }

    $app3 = Restart-ProbeApp $Language
    if (-not $app3) { Bail '  the second relaunch produced no window' }
    Say ('  restored: window [' + (Window $app3.Id).Current.Name + ']')
    Say ('  menu bar now: |' + ((MenuBarNames $app3.Id) -join '| |') + '|')
    Say ('  the app reports [' + (LanguageLabel $app3.Id 'after the restore') + ']')
    @(LanguageRows $app3.Id 'after the restore') | Out-Null
  }
  'openpicker' {
    # Where does our own open dialog start when the program is given no directory at all?
    # sec.48 left that half-unjudged: two channels could put a folder in front of the reader --
    # the one we store (files/lastOpenDirectory, which mainwindow.cpp:1554 hands the dialog
    # explicitly, falling back to QDir::homePath()) and the shell's per-application dialog memory
    # (HKCU:\...\Explorer\ComDlg32\LastVisitedPidlMRU, filed under the EXE NAME, which both this
    # dev build and the packaged copy share because both are called xyzpdf.exe).
    #
    # Reading the registry cannot decide it, and the first version of this note claimed it could:
    # every instance this script launches gets XYZPDF_SETTINGS_PATH pointed at a scratch ini, so
    # the dev build's HKCU:\Software\xyzpdf value is untouched by these runs -- it is a residue of
    # someone launching the exe by hand. The discriminating run is instead: launch WITHOUT -LastDir
    # (then the ini has no [files] section at all, our key reads empty, and the code says
    # "homePath"), and see which folder the dialog shows. Home means our explicit directory wins;
    # the folder this app last saved into means the shell's memory does.
    $pk = ShowOpenDialog $app.Id
    if (-not $pk) { Bail '  the open dialog never came up in three tries' }
    # The dialog's own folder is not exposed as a readable Name anywhere (sec.29: the picker gives
    # no patterns; sec.44: its text fields answer empty to WM_GETTEXT), so the FOLDER is read from
    # what is inside it -- the rows. Home and a scratch folder look nothing like each other.
    $rows = @(FindType $pk $CT::ListItem)
    Say ('  ' + $rows.Count + ' row(s) in the folder the dialog opened at:')
    foreach ($e in ($rows | Select-Object -First 24)) { Say ('    row |' + $e.Current.Name + '|') }
    # Also print the tree of headers/edit boxes: the address band sometimes reports the path there
    # where it reports nothing in the name field.
    foreach ($h in @(FindType $pk $CT::Header)) { Say ('  header |' + $h.Current.Name + '|') }
    foreach ($t in @(FindType $pk $CT::Edit)) {
      $idh = [W32]::GetDlgCtrlID([IntPtr]$t.Current.NativeWindowHandle)
      Say ('  edit id=' + $idh + ' answers |' + [W32]::Text([IntPtr]$t.Current.NativeWindowHandle) + '|')
    }
    # Close it without choosing anything: control id 2 is Cancel in a #32770, and BM_CLICK is
    # honoured by the OPEN dialog (sec.44 measured that the SAVE dialog is the one that ignores it).
    $kids = New-Object System.Collections.Generic.List[IntPtr]
    $cb = [W32+EnumProc] { param($h, $l) $kids.Add($h); return $true }
    [W32]::EnumChildWindows([IntPtr]$pk.Current.NativeWindowHandle, $cb, [IntPtr]::Zero) | Out-Null
    $cancel = $null
    foreach ($h in $kids) {
      if ([W32]::Cls($h) -eq 'Button' -and [W32]::GetDlgCtrlID($h) -eq 2) { $cancel = $h }
    }
    if ($cancel) {
      [W32]::PostMessageW($cancel, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
      Start-Sleep -Milliseconds 800
      Say '  cancelled without picking'
    } else {
      Say '  no Cancel button with control id 2; leaving the dialog open and saying so'
    }
  }
  'pathforms' {
    # sec.88 and sec.90 established a family: a SHELL input box refuses a path spelled with forward
    # slashes, while our own Qt directory box has accepted that spelling for slices. Both rounds
    # recorded the refusal; neither asked WHICH PART of the string the parser chokes on, because the
    # shell's source is not readable from here and inventing a reason is what this suite keeps
    # catching itself doing. What CAN be measured is the boundary. One existing file is spelled six
    # ways, each written raw (no normalisation -- the spelling is the variable) into a named control of
    # the open dialog on a fresh instance, and every row prints the two receipts that are not the click:
    # did OUR window title take the target's file name, and did the dialog close. Rows that must refuse
    # are asserted, so this table can go red in both directions:
    #   R1 must land      -- if it does not, this harness cannot open anything through its own picker
    #                         and every other row here measures nothing
    #   R2 must not land  -- if it does, sec.90's reading about the File name box is what is wrong
    #   R5 must not land  -- a path to a file that does not exist; if THIS lands, the "landed" test is
    #                         not watching a real open (sec.90's rule: a posted click is not acceptance)
    # R3/R4/R6 carry no expectation: they are the boundary itself -- one slash among backslashes, a
    # folder spelled with slashes and nothing else, and the address band (id 1001) given the same
    # string the name box (1148) refused.
    if (-not $Target) { Bail '  pathforms needs -Target <a pdf that exists>; -Doc stays the decoy' }
    if (-not (Test-Path -LiteralPath $Target)) { Bail ('  no file at the -Target path: ' + $Target) }
    $tLeaf = [System.IO.Path]::GetFileName($Target)
    $dLeaf = [System.IO.Path]::GetFileName($Doc)
    if ($tLeaf -eq $dLeaf) {
      Bail ('  -Doc and -Target name the same file (' + $tLeaf + '): the instance already shows that ' +
            'name in its title bar, so the landed-check would be true before the dialog is touched')
    }
    $tDir = [System.IO.Path]::GetDirectoryName($Target)
    $rows = @(
      [pscustomobject]@{ tag = 'R1-backslash-full'; id = 1148; text = $Target;                              expect = 'land'   },
      [pscustomobject]@{ tag = 'R2-all-slash';      id = 1148; text = $Target.Replace('\', '/');             expect = 'refuse' },
      [pscustomobject]@{ tag = 'R3-slash-before-leaf'; id = 1148; text = ($tDir + '/' + $tLeaf);             expect = 'none'   },
      [pscustomobject]@{ tag = 'R4-slash-folder';   id = 1148; text = ($tDir.Replace('\', '/') + '/');       expect = 'none'   },
      [pscustomobject]@{ tag = 'R5-file-missing';   id = 1148; text = ($tDir + '\sec97-no-such-file.pdf');   expect = 'refuse' },
      [pscustomobject]@{ tag = 'R6-address-band';   id = 1001; text = $Target.Replace('\', '/');             expect = 'none'   }
    )
    foreach ($r in $rows) {
      $app = Restart-ProbeApp $Language
      if (-not $app) { Bail ('  ' + $r.tag + ': the relaunch produced no window, so this row was not measured') }
      Say ('-- ' + $r.tag + '  writing into control id ' + $r.id + '  expect=' + $r.expect)
      $pk = ShowOpenDialog $app.Id
      if (-not $pk) { Bail ('  ' + $r.tag + ': the open dialog never came up in three tries') }
      if (-not (SetPickerFieldRaw $pk $r.id $r.text)) { Bail ('  ' + $r.tag + ': control id ' + $r.id + ' is not there to write into') }
      if (-not (PressPickerAccept $pk)) { Bail ('  ' + $r.tag + ': the dialog has no accept button to press') }
      $landed = $false
      $closed = $false
      $title = ''
      for ($k = 1; $k -le 6; $k++) {
        Start-Sleep -Milliseconds 500
        $w = Window $app.Id
        if ($w) { $title = $w.Current.Name; if ($title -like ('*' + $tLeaf + '*')) { $landed = $true } }
        if (-not (FindPicker $app.Id)) { $closed = $true }
        if ($landed -and $closed) { break }
      }
      $navigated = $false
      $still = FindPicker $app.Id
      if ($still) {
        foreach ($e in @(FindType $still $CT::ListItem)) {
          if ($e.Current.Name -like ('*' + $tLeaf + '*')) { $navigated = $true }
        }
      }
      Say ('   text [' + $r.text + ']')
      Say ('   landed=' + $landed + ' pickerClosed=' + $closed + ' folderShowsTarget=' + $navigated +
           '  title [' + $title + ']')
      if ($r.expect -eq 'land' -and -not $landed) {
        Bail ('  ' + $r.tag + ': the backslash form did NOT reach our window, so this harness cannot open a ' +
              'document through its own picker and no other row in this table is evidence of anything')
      }
      if ($r.expect -eq 'refuse' -and $landed) {
        Bail ('  ' + $r.tag + ': this form OPENED a document [' + $title + ']. Either the slash-path reading ' +
              '(sec.90) or the missing-file control is wrong, and they cannot both stand')
      }
    }
    Say ('  table done: ' + $rows.Count + ' spellings of [' + $tLeaf + '] through control ids 1148/1001')
  }
  'formpage' {
    # Does this reader DRAW a form field at all? Everything sec.46 to sec.53 measured lives inside the
    # file; not one round of this suite has looked at a page carrying a widget. So: open one
    # document, raise the window (a real click on its own title bar -- SetForegroundWindow does
    # not move it from this harness), crop the page area and count non-white points per horizontal
    # band. Run the mode twice, once on the plain file and once on the same page with a field on
    # it, and subtract: a band that differs is where something is drawn, and no band differing is
    # the answer to the question. Bands rather than one total because the total can hide a small
    # box under page text -- sec.28's lesson about what a "soft edge" number really measures.
    $win = $null
    for ($k = 1; $k -le 10 -and -not $win; $k++) {
      Start-Sleep -Milliseconds 500
      $win = Window $app.Id
    }
    if (-not $win) { Bail '  no window' }
    $wr = $win.Current.BoundingRectangle
    [W32]::Click([int]($wr.X + $wr.Width / 2), [int]($wr.Y + 12))   # raise it by its own title bar
    Start-Sleep -Milliseconds 800
    $win = Window $app.Id
    $hwnd = [IntPtr]$win.Current.NativeWindowHandle
    $scale = [W32]::GetDpiForWindow($hwnd) / 96.0
    # UIA's top-level rectangle is the CLIENT area; the picture PrintWindow fills is the WHOLE
    # window, whose top-left is the frame (sec.74 said the bitmap has to be the outer size, and this
    # mode's first PrintWindow version still cropped it with client-relative arithmetic -- the same
    # lesson, applied to the origin instead of the size). Both are screen pixels, so the two sources
    # have to agree here; if they do not, one of them is speaking logical pixels and every number
    # below is off by the DPI factor. This machine's window has read scale 1 in every round, so the
    # check is what would surface that, not a claim that it was measured.
    $gr = [W32]::Rect($hwnd)
    $co = [W32]::ClientOrigin($hwnd)
    $dx = [int]$wr.X - $co.x
    $dy = [int]$wr.Y - $co.y
    Say ('  window rect ' + ($gr.right - $gr.left) + 'x' + ($gr.bottom - $gr.top) + ' at ' +
         $gr.left + ',' + $gr.top + '; client origin ' + $co.x + ',' + $co.y + '; UIA says ' +
         [int]$wr.X + ',' + [int]$wr.Y + ' (deltas ' + $dx + ',' + $dy + '), scale ' + $scale)
    if ([math]::Abs($dx) -gt 2 -or [math]::Abs($dy) -gt 2) {
      Bail ('  the top-level UIA rectangle disagrees with the Win32 client origin by ' + $dx + ',' +
            $dy + ' px -- the crop and the picture are not in the same units')
    }
    # Print the candidates before picking one: which element is the page has been guessed wrong
    # twice in this suite, and a guess that fails quietly reads as "nothing was drawn".
    $best = $null; $bestTop = -1; $maxArea = 0
    $cands = New-Object System.Collections.Generic.List[object]
    foreach ($type in @($CT::Document, $CT::Pane, $CT::Image)) {
      foreach ($e in @(FindType $win $type)) {
        $r = $e.Current.BoundingRectangle
        $area = [int]($r.Width * $r.Height)
        if ($area -gt 40000) {
          Say ('  candidate ' + $type.ProgrammaticName + ' ' + [int]$r.X + ',' + [int]$r.Y + ' ' + [int]$r.Width + 'x' + [int]$r.Height)
          $cands.Add($e)
          if ($area -gt $maxArea) { $maxArea = $area }
        }
      }
    }
    # Nested Panes: the OUTER one starts under the toolbar and runs to the window edge, so its top
    # 36 rows are the tab strip -- and the tab strip carries the document name, which differs
    # between two files. That is how a plain A/B of "field drawn vs not drawn" first reported 681
    # differing pixels for a box that was never drawn: the difference was the file name in the tab.
    # The page view is the INNERMOST of the large panes, so pick by the lowest top edge among
    # anything within 60% of the largest area, and keep printing every candidate so the choice is
    # falsifiable rather than a guess with a rule bolted on.
    foreach ($e in $cands) {
      $r = $e.Current.BoundingRectangle
      if (($r.Width * $r.Height) -lt (0.6 * $maxArea)) { continue }
      if ([int]$r.Y -gt $bestTop) { $bestTop = [int]$r.Y; $best = $e }
    }
    if (-not $best) { Bail '  no element big enough to be the page' }
    $r = $best.Current.BoundingRectangle
    # PrintWindow, not CopyFromScreen. sec.81 measured what the screen route does on this machine:
    # four different documents produced *identical* band counts and *four different* PNG hashes,
    # because a topmost third-party window was sitting inside the crop and what varied was that
    # window's own clock. Asking the window to paint itself cannot be covered up. The framing is
    # the outer window rect (sec.74's lesson: the bitmap has to be the size PrintWindow fills, and a
    # crop that disagrees with it silently cuts a row off); the geometry is printed so a wrong crop
    # says so.
    $fw = [int]($gr.right - $gr.left)
    $fh = [int]($gr.bottom - $gr.top)
    $winBmp = New-Object System.Drawing.Bitmap($fw, $fh)
    $wg = [System.Drawing.Graphics]::FromImage($winBmp)
    $hdc = $wg.GetHdc()
    # 2 = PW_RENDERFULLCONTENT, the flag a DirectComposition-backed window needs.
    $painted = [W32]::PrintWindow($hwnd, $hdc, 2)
    $wg.ReleaseHdc($hdc)
    $winShot = Join-Path $scratch ('formpage-win-' +
                                   [System.IO.Path]::GetFileNameWithoutExtension($Doc) + '.png')
    $winBmp.Save($winShot, [System.Drawing.Imaging.ImageFormat]::Png)
    $wg.Dispose()
    Say ('  PrintWindow returned ' + $painted + ', window ' + $fw + 'x' + $fh + ' kept at ' + $winShot)
    # Window-local, not client-local: bitmap row 0 is the frame's top edge.
    $left = [int][math]::Floor($r.X - $gr.left)
    $top = [int][math]::Floor($r.Y - $gr.top)
    $width = [int][math]::Ceiling($r.Width)
    $height = [int][math]::Ceiling($r.Height)
    Say ('  page element ' + $best.Current.ControlType.ProgrammaticName + ' cropped at ' + $left +
         ',' + $top + ' ' + $width + 'x' + $height + ' (frame ' + ($co.x - $gr.left) + ',' +
         ($co.y - $gr.top) + ')')
    if ($left -lt 0) { $width += $left; $left = 0 }
    if ($top -lt 0) { $height += $top; $top = 0 }
    if ($left + $width -gt $fw) { $width = $fw - $left }
    if ($top + $height -gt $fh) { $height = $fh - $top }
    if ($width -lt 100 -or $height -lt 100) {
        $winBmp.Dispose()
        Bail ('  the page crop is ' + $width + 'x' + $height + ' inside a ' + $fw + 'x' + $fh +
              ' window picture -- the geometry disagrees, so no band number from it means anything')
    }
    $bmp = $winBmp.Clone((New-Object System.Drawing.Rectangle($left, $top, $width, $height)),
                         $winBmp.PixelFormat)
    $winBmp.Dispose()
    # Two guards, because this mode's whole job is to say "nothing was drawn" and both of its known
    # ways to lie are silent. (1) The crop must begin INSIDE the client area: the picture is anchored
    # to the window frame, so a crop computed from the client rectangle begins on 8 px of unpainted
    # border and 31 rows above the element it names (sec.89). Said geometrically, not by colour --
    # sec.91 first wrote "a column of near-black is the frame" and that refused a legitimately dark
    # sidebar, which is a colour assumption about the user's theme, not a fact about the window.
    # (2) The tab strip carries the document name, and that name differs between the two pictures
    # this mode subtracts -- sec.81's 681 false pixels. So the crop must
    # start below the bottom of every tab, and the check is done with the tabs' own rectangles.
    if ($left -lt ($co.x - $gr.left) -or $top -lt ($co.y - $gr.top)) {
        $bmp.Dispose()
        Bail ('  the crop starts at bitmap ' + $left + ',' + $top + ' but the client area only begins ' +
              'at ' + ($co.x - $gr.left) + ',' + ($co.y - $gr.top) + ' -- that picture is anchored to ' +
              'the window frame, so these bands are not the page')
    }
    $tabBottom = -1
    foreach ($t in @(FindType $win $CT::TabItem)) {
      $tr = $t.Current.BoundingRectangle
      if ([int]$tr.Bottom -gt $tabBottom) { $tabBottom = [int]$tr.Bottom }
    }
    if ($tabBottom -ge 0 -and $top -lt ($tabBottom - $gr.top)) {
        $bmp.Dispose()
        Bail ('  the crop starts at window row ' + $top + ', above the bottom of the tab strip (' +
              ($tabBottom - $gr.top) + ') -- a difference between two documents there is their file ' +
              'names, not the page')
    }
    Say ('  tab strip bottom at window row ' + $(if ($tabBottom -ge 0) { $tabBottom - $gr.top } else { '(no tabs)' }) +
         ', crop starts at ' + $top)
    # Named after the document, not a fixed file: an A/B of two runs is only evidence if both
    # pictures survive, and a shared name means the second run quietly ate the first one's.
    $shot = Join-Path $scratch ('formpage-' + [System.IO.Path]::GetFileNameWithoutExtension($Doc) + '.png')
    $bmp.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
    $bands = 12
    for ($b = 0; $b -lt $bands; $b++) {
      $y0 = [int]($height * $b / $bands)
      $y1 = [int]($height * ($b + 1) / $bands)
      $ink = 0
      for ($y = $y0; $y -lt $y1; $y += 2) {
        for ($x = 0; $x -lt $width; $x += 2) {
          $c = $bmp.GetPixel($x, $y)
          if ($c.R -lt 245 -or $c.G -lt 245 -or $c.B -lt 245) { $ink++ }
        }
      }
      Say ('  band ' + $b + ' rows ' + $y0 + '-' + $y1 + ' ink=' + $ink)
    }
    $bmp.Dispose()
    Say ('  shot kept at ' + $shot)
  }
  'uacache' {
    # sec.37 recorded that a status-bar message's UIA Name can lag what is on screen and blamed the
    # .NET UIA client's property cache -- without ever testing the attribution. Two rounds also
    # disagreed (one refreshed in 7 s, one held a 20-second-old line for 24 s), which is the shape a
    # comfortable wrong explanation absorbs. So: read the SAME instant four ways and let pixels
    # arbitrate.
    #   held  -- one element captured before the message ends and re-read every sample;
    #   fresh -- the status bar found again from the window each sample (what StatusName does);
    #   root  -- a brand-new search from the desktop root, so a per-element cache has nowhere to sit;
    #   ink   -- the truth: non-white points in the message area, grabbed off the screen.
    # The message is the one the app shows by itself when a document opens (`Opened <name>`, 6 s),
    # which means no dialog, no file and no picker is in front of the strip while it is measured --
    # the export route the first version of this mode used needed all three and produced two rounds
    # of nothing but a covered status bar.
    $wr = (Window $app.Id).Current.BoundingRectangle
    [W32]::Click([int]($wr.X + $wr.Width / 2), [int]($wr.Y + 12))   # raise by its own title bar
    Start-Sleep -Milliseconds 600
    $heldList = @(FindType (Window $app.Id) $CT::StatusBar)
    if ($heldList.Count -lt 1) { Bail '  no StatusBar element to hold' }
    $heldElem = $heldList[0]
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    for ($k = 1; $k -le 44; $k++) {
      $h = ''
      try { $h = $heldElem.Current.Name } catch { $h = '(threw)' }
      $f = StatusName $app.Id
      $rootName = ''
      $top = $AE::RootElement.FindFirst($TS::Children, (New-Object $PC($AE::ProcessIdProperty, $app.Id)))
      if ($top) { $sb = @(FindType $top $CT::StatusBar); if ($sb.Count) { $rootName = $sb[0].Current.Name } }
      $ink = StatusInk $app.Id ''
      Say ('  t=' + [math]::Round($clock.Elapsed.TotalSeconds, 1) + 's ink=' + $ink +
           ' held=[' + $h.Substring(0, [math]::Min(27, $h.Length)) + ']' +
           ' fresh=[' + $f.Substring(0, [math]::Min(27, $f.Length)) + ']' +
           ' root=[' + $rootName.Substring(0, [math]::Min(27, $rootName.Length)) + ']')
      Start-Sleep -Milliseconds 500
    }
  }
  'outlinetree' {
    # sec.64 fixed the bookmark tree read on file-level evidence, and its own record says the screen was
    # never looked at. So ask the window: with a document that has a real outline open, how many rows does
    # the tree element expose, how many of them belong to the root, and do the nested ones exist on screen
    # at all. The numbers to compare against are the file's: tests/outlineprobe.cpp section D counted 184
    # items with 14 under the root for this very document.
    #
    # Every count is printed beside a shape that distinguishes it, because "no rows" here has three causes
    # and two of them are the instrument: the sidebar is hidden until asked for (documenttab.cpp:113), the
    # panel lands on the bookmark tab only when the document has bookmarks (documenttab.cpp:652), and a
    # tree that is not the current tab has no rows to report. So the tab labels, the TogglePattern state of
    # the row that opens the sidebar, and the tree's own rectangle are printed before any count is believed.
    #
    # The digit needle is what makes a nested read falsifiable rather than a bigger number: on this book the
    # 14 roots are chapter headings (none starts with a digit) and the 170 nested ones are numbered sections
    # (1.1, 10.9.2). A root-only read cannot produce a single digit-starting name, whatever else it shows.
    $win = $null
    for ($k = 1; $k -le 10 -and -not $win; $k++) {
      Start-Sleep -Milliseconds 500
      $win = Window $app.Id
    }
    if (-not $win) { Bail '  no window' }
    $wr = $win.Current.BoundingRectangle
    [W32]::Click([int]($wr.X + $wr.Width / 2), [int]($wr.Y + 12))   # raise it by its own title bar
    Start-Sleep -Milliseconds 800
    $win = Window $app.Id

    $outlineNeedles = @('Outline', (CjStr 0x5927, 0x7EB2))
    $tabs = @(FindType $win $CT::TabItem)
    foreach ($t in $tabs) {
      $tr = $t.Current.BoundingRectangle
      Say ('  tab |' + $t.Current.Name + '| at ' + [int]$tr.X + ',' + [int]$tr.Y + ' ' +
           [int]$tr.Width + 'x' + [int]$tr.Height)
    }
    Say ('  ' + $tabs.Count + ' tab item(s), ' + @(FindType $win $CT::Tree).Count + ' tree element(s)')

    # What counts as "the sidebar is up" is read two ways, because the first round of this mode learned
    # that neither alone is enough: the bookmark panel shows as a TabItem labelled 'Outline' (its Chinese
    # form is 0x5927 0x7EB2) only if Qt exposes that QTabBar, and as a Tree element only if that panel is
    # the current one. A round that finds neither is an input round that did not land (sec.41), not a
    # document with no bookmarks, so each attempt below prints both counts.
    $tab = $null
    $tree = $null
    function FindOutlineTab($w, [string[]]$needles) {
      foreach ($t in @(FindType $w $CT::TabItem)) {
        foreach ($n in $needles) { if ($t.Current.Name -eq $n) { return $t } }
      }
      return $null
    }
    $tab = FindOutlineTab $win $outlineNeedles
    $found = @(FindType $win $CT::Tree)
    if ($found.Count -gt 0) { $tree = $found[0] }

    if (-not $tab -and -not $tree) {
      # The sidebar is closed (documenttab.cpp:113 hides it until asked for). Open it through its own
      # menu row -- a checkable QAction reaches UIA as TogglePattern (sec.49), so its state is read on
      # both sides of each attempt rather than assumed.
      # The bar's label is 0x67E5 0x770B with (&V) -- "look", not the 0x89C6 0x56FE ("view") this mode's
      # first round asked for; that round reported "no top-level menu matching", which is the instrument
      # failing, not the app having no menu.
      $rowNeedles = @('Navigation Sidebar', (CjStr 0x5BFC, 0x822A, 0x4FA7, 0x680F))
      # Two channels, tried in this order, for a reason this mode measured rather than guessed: a
      # TogglePattern call on a row of an OPEN Qt menu does nothing at all (the round above: toggle=Off
      # before, nothing after), and it leaves the popup up. A popup still open is what makes the next
      # attempt lie -- re-asking the bar for a menu that is already open CLOSES it (sec.56), so the row
      # is then gone and "no Navigation Sidebar row" reads like the app has none. Hence every attempt
      # first collapses whatever is expanded, and the real click goes first because it is the channel
      # that has worked for menu rows since sec.34.
      foreach ($way in @('real click', 'toggle-pattern')) {
        foreach ($bar in @(FindType $win $CT::MenuBar)) {
          foreach ($t in @($bar.FindAll($TS::Children, $ANY))) {
            $xp = Pat $t 'ExpandCollapse'
            if ($xp -and $xp.Current.ExpandCollapseState.ToString() -eq 'Expanded') {
              try { $xp.Collapse(); Say ('  collapsed an open [' + $t.Current.Name + '] first') } catch { }
            }
          }
        }
        Start-Sleep -Milliseconds 400
        TopMenu $app.Id @('View', (CjStr 0x67E5, 0x770B)) | Out-Null
        $row = RowAny $app.Id $rowNeedles
        if (-not $row) { Say ('  ' + $way + ': no Navigation Sidebar row in the View menu'); continue }
        $tg = Pat $row 'Toggle'
        $before = '(no TogglePattern)'
        if ($tg) { $before = $tg.Current.ToggleState.ToString() }
        $rr = $row.Current.BoundingRectangle
        Say ('  ' + $way + ': aiming at |' + $row.Current.Name + '| at ' + [int]$rr.X + ',' + [int]$rr.Y +
             ' enabled=' + $row.Current.IsEnabled + ' toggle=' + $before)
        if ($way -eq 'toggle-pattern') {
          if (-not $tg) { Say '  no TogglePattern to call'; continue }
          try { $tg.Toggle() } catch { Say ('  Toggle threw: ' + $_.Exception.Message) }
        } else {
          ClickAt $row
        }
        for ($k = 1; $k -le 6 -and -not $tab -and -not $tree; $k++) {
          Start-Sleep -Milliseconds 500
          $win = Window $app.Id
          $tab = FindOutlineTab $win $outlineNeedles
          $found = @(FindType $win $CT::Tree)
          if ($found.Count -gt 0) { $tree = $found[0] }
        }
        Say ('  ' + $way + ' gave ' + @(FindType $win $CT::TabItem).Count + ' tab item(s), ' +
             $found.Count + ' tree element(s)')
        if ($tab -or $tree) { break }
      }
    }
    if (-not $tab -and -not $tree) {
      Say '  the sidebar did not come up -- every name this window has, so a dead round is a reading'
      $seen = @{}
      foreach ($e in @($win.FindAll($TS::Descendants, $ANY))) {
        $nm = $e.Current.Name
        if ($null -eq $nm) { continue }
        $nm = $nm.Replace("`r", ' ').Replace("`n", ' ').Trim()
        if ($nm.Length -lt 2) { continue }
        $key = $e.Current.ControlType.ProgrammaticName.Replace('ControlType.', '') + ' |' + $nm + '|'
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = 1
        Say ('    ' + $key)
      }
      Bail 'the sidebar did not come up -- the names printed above are what this window offered'
    }

    if (-not $tree) {
      # A tab but no tree: the panel is up on the other page, so make this one current.
      for ($k = 1; $k -le 6 -and -not $tree; $k++) {
        ClickAt $tab
        Start-Sleep -Milliseconds 700
        $win = Window $app.Id
        $found = @(FindType $win $CT::Tree)
        if ($found.Count -gt 0) { $tree = $found[0] }
      }
    }
    if (-not $tree) { Bail '  no Tree element on screen' }
    $r = $tree.Current.BoundingRectangle
    Say ('  tree element at ' + [int]$r.X + ',' + [int]$r.Y + ' ' + [int]$r.Width + 'x' + [int]$r.Height)

    # Ask the same question through two more patterns before trusting a count of found elements: this
    # widget answers Grid and Table but not Scroll, and the rows UIA hands back are the realised ones.
    # A grid's own RowCount is the widget's answer about its model, which is what "how many bookmarks are
    # on screen" was meant to measure. Reading several channels at once is sec.56's rule.
    $gp = Pat $tree 'Grid'
    if ($gp) {
      Say ('  GridPattern rows=' + $gp.Current.RowCount + ' columns=' + $gp.Current.ColumnCount)
    }
    $tp = Pat $tree 'Table'
    if ($tp) {
      Say ('  TablePattern rows=' + $tp.Current.RowCount)
    }
    $sp = Pat $tree 'Selection'
    if ($sp) {
      Say ('  SelectionPattern holds ' + $sp.Current.GetSelection().Length + ' selected item(s)')
    }

    $all = @(FindType $tree $CT::TreeItem)
    $rootRows = @($tree.FindAll($TS::Children, (New-Object $PC($AE::ControlTypeProperty, $CT::TreeItem))))
    $named = @($all | Where-Object { $_.Current.Name.Trim().Length -gt 0 })
    $digits = @($all | Where-Object { $_.Current.Name -match '^[0-9]' })
    Say ('  TreeItem descendants=' + $all.Count + '  named=' + $named.Count +
         '  root children=' + $rootRows.Count + '  digit-starting=' + $digits.Count)
    foreach ($e in @($all | Select-Object -First 18)) {
      $kids = @($e.FindAll($TS::Children, (New-Object $PC($AE::ControlTypeProperty, $CT::TreeItem))))
      Say ('  row children=' + $kids.Count + ' |' + $e.Current.Name.Trim() + '|')
    }
    if ($rootRows.Count -gt 0) {
      $ex = Pat $rootRows[0] 'ExpandCollapse'
      $state = '(no ExpandCollapsePattern)'
      if ($ex) { $state = $ex.Current.ExpandCollapseState.ToString() }
      Say ('  first root row expand state: ' + $state)
      # QTreeView::expandToDepth(0) leaves exactly one level open (outlinepanel.cpp:45), so the deepest
      # rows are collapsed by design. Open every root row and recount, so "the tree holds 184" and "the
      # screen shows 103 because two are folded" stay two separate readings.
      $opened = 0
      foreach ($e in $rootRows) {
        $ex = Pat $e 'ExpandCollapse'
        if ($ex -and $ex.Current.ExpandCollapseState.ToString() -ne 'Expanded') {
          try { $ex.Expand(); $opened++ } catch { }
        }
      }
      Start-Sleep -Milliseconds 900
      $all2 = @(FindType $tree $CT::TreeItem)
      $digits2 = @($all2 | Where-Object { $_.Current.Name -match '^[0-9]' })
      Say ('  after expanding ' + $opened + ' root row(s): descendants=' + $all2.Count +
           ' digit-starting=' + $digits2.Count)
    }

    # The count above is the VIEWPORT, not the document: Qt hands UIA only the rows it has realised, so a
    # 184-row tree shows ~26 at a time and expanding a collapsed row swaps rows in and out instead of
    # adding any. Measured, not assumed -- the expand step changed nothing. So sweep the scroll position
    # and accumulate distinct names, which is the only way this channel can answer "all of them on screen".
    $scroll = Pat $tree 'Scroll'
    if (-not $scroll) {
      # Print what the element does support instead of silently reporting the viewport count: whether a
      # channel exists at all is the first question here, and a pattern that is absent looks exactly like
      # a tree with nothing to show.
      $have = @($tree.GetSupportedPatterns() | ForEach-Object { $_.ProgrammaticName.Replace('PatternIdentifiers.', '') })
      Say ('  the tree element supports: ' + ($have -join ', '))
      $scrollers = @()
      foreach ($e in @($win.FindAll($TS::Descendants, $ANY))) {
        if (Pat $e 'Scroll') { $scrollers += $e }
      }
      Say ('  ' + $scrollers.Count + ' element(s) in this window carry a ScrollPattern')
      foreach ($e in @($scrollers | Select-Object -First 4)) {
        $sr = $e.Current.BoundingRectangle
        Say ('    scroller ' + $e.Current.ControlType.ProgrammaticName + ' |' + $e.Current.Name + '| at ' +
             [int]$sr.X + ',' + [int]$sr.Y + ' ' + [int]$sr.Width + 'x' + [int]$sr.Height)
      }
      if ($scrollers.Count -gt 0) {
        foreach ($e in $scrollers) {
          $sr = $e.Current.BoundingRectangle
          if ($sr.Width -gt 40 -and $sr.X -lt ($r.X + $r.Width)) { $scroll = Pat $e 'Scroll'; break }
        }
      }
    }
    if ($scroll) {
      $seenNames = @{}
      $steps = 14
      for ($s = 0; $s -le $steps; $s++) {
        try {
          $scroll.SetHorizontalScrollPercent(-1)
          $scroll.SetVerticalScrollPercent([double]($s * 100 / $steps))
        } catch {
          Say ('  SetVerticalScrollPercent threw: ' + $_.Exception.Message)
          break
        }
        Start-Sleep -Milliseconds 350
        foreach ($e in @(FindType $tree $CT::TreeItem)) {
          $nm = $e.Current.Name.Trim()
          if ($nm.Length -gt 0) { $seenNames[$nm] = 1 }
        }
      }
    } else {
      # No scroll channel at all, so drive the widget the way a reader does: a real click inside it to
      # take focus, then page it from the keyboard. SendKeys reaches the foreground window, which is why
      # the title-bar raise at the top of this mode is not decoration.
      Add-Type -AssemblyName System.Windows.Forms
      ClickAt $tree
      Start-Sleep -Milliseconds 500
      $seenNames = @{}
      $stale = 0
      for ($s = 1; $s -le 24 -and $stale -lt 3; $s++) {
        [System.Windows.Forms.SendKeys]::SendWait('{PGDN}')
        Start-Sleep -Milliseconds 300
        $before = $seenNames.Keys.Count
        foreach ($e in @(FindType $tree $CT::TreeItem)) {
          $nm = $e.Current.Name.Trim()
          if ($nm.Length -gt 0) { $seenNames[$nm] = 1 }
        }
        if ($seenNames.Keys.Count -eq $before) { $stale++ } else { $stale = 0 }
      }
      [System.Windows.Forms.SendKeys]::SendWait('^({END})')
      Start-Sleep -Milliseconds 500
      foreach ($e in @(FindType $tree $CT::TreeItem)) {
        $nm = $e.Current.Name.Trim()
        if ($nm.Length -gt 0) { $seenNames[$nm] = 1 }
      }
      Say ('  paged with the keyboard: ' + $seenNames.Keys.Count + ' distinct row name(s)')
    }
    $digitSeen = @($seenNames.Keys | Where-Object { $_ -match '^[0-9]' })
    Say ('  the sweep accumulated ' + $seenNames.Keys.Count +
         ' distinct row name(s), ' + $digitSeen.Count + ' of them starting with a digit')
    foreach ($nm in @($seenNames.Keys | Select-Object -Last 3)) { Say ('  sample |' + $nm + '|') }
  }
  'tabstrip' {
    # Crop the tab strip and keep the picture. Everything else this harness does is a count, and a
    # count cannot see which glyph a stylesheet rule drew: sec.66's close button was the combo box's
    # down arrow, and the only reading that could tell was the pixels. So print the geometry the crop
    # came from (a crop of the wrong rectangle looks exactly like a tab with no button on it), and
    # name the file after the document so two runs do not eat each other's picture.
    $win = $null
    for ($k = 1; $k -le 10 -and -not $win; $k++) {
      Start-Sleep -Milliseconds 500
      $win = Window $app.Id
    }
    if (-not $win) { Bail '  no window' }
    $wr = $win.Current.BoundingRectangle
    # Pixels need the z-order, which UIA does not: the first round of this mode cropped a tab strip
    # and got the IDE that was sitting on top of our window at those coordinates. SwitchToThisWindow
    # is the Alt+Tab call and crosses processes without the foreground-ownership fight that
    # SetForegroundWindow has; the title-bar click that follows is the old route, kept because it
    # also wakes the window. And the foreground title is printed, so a crop of somebody else's
    # window says so in the log instead of reading as "a tab with no button on it".
    $hwnd = [IntPtr]$win.Current.NativeWindowHandle
    [W32]::SwitchToThisWindow($hwnd, $true)
    Start-Sleep -Milliseconds 500
    Say ('  foreground before the click [' + [W32]::Text([W32]::GetForegroundWindow()) + ']')
    [W32]::Click([int]($wr.X + $wr.Width / 2), [int]($wr.Y + 12))   # raise it by its own title bar
    Start-Sleep -Milliseconds 800
    $win = Window $app.Id
    Say ('  foreground after the click  [' + [W32]::Text([W32]::GetForegroundWindow()) + ']')
    $scale = [W32]::GetDpiForWindow([IntPtr]$win.Current.NativeWindowHandle) / 96.0

    $tab = $null
    foreach ($t in @(FindType $win $CT::TabItem)) {
      $tr = $t.Current.BoundingRectangle
      Say ('  tab |' + $t.Current.Name + '| at ' + [int]$tr.X + ',' + [int]$tr.Y + ' ' +
           [int]$tr.Width + 'x' + [int]$tr.Height)
      if ($t.Current.Name -like '*.pdf*' -and -not $tab) { $tab = $t }
    }
    if (-not $tab) { Bail '  no document tab to crop' }

    $r = $tab.Current.BoundingRectangle

    # The window asked to paint itself into a bitmap rather than grabbed off the screen, because
    # z-order is not a fight this harness wins: the round before this one captured the tab strip and
    # got a third-party trial dialog that sits above every window on the desktop, and the foreground
    # title said our window was up while its pixels were not. PrintWindow asks the window itself.
    #
    # Sized AND cropped by the OUTER window rect. This mode had the same wrong anchor sec.89 found in
    # formpage: the picture is anchored to the frame while the arithmetic subtracted UIA's client
    # rectangle, so the crop started 8 px left (on the frame) and 31 rows too high -- which for a
    # 41-row crop of a 35-row tab meant most of what it printed was the toolbar above the strip.
    $gr = [W32]::Rect($hwnd)
    $co = [W32]::ClientOrigin($hwnd)
    $fw = [int]($gr.right - $gr.left)
    $fh = [int]($gr.bottom - $gr.top)
    $winBmp = New-Object System.Drawing.Bitmap($fw, $fh)
    $wg = [System.Drawing.Graphics]::FromImage($winBmp)
    $hdc = $wg.GetHdc()
    # 2 = PW_RENDERFULLCONTENT, which is the flag a DirectComposition-backed window needs.
    $painted = [W32]::PrintWindow($hwnd, $hdc, 2)
    $wg.ReleaseHdc($hdc)
    $winShot = Join-Path $scratch ('tabstrip-win-' +
                                   [System.IO.Path]::GetFileNameWithoutExtension($Doc) + '.png')
    $winBmp.Save($winShot, [System.Drawing.Imaging.ImageFormat]::Png)
    $wg.Dispose()
    Say ('  PrintWindow returned ' + $painted + ', window ' + $fw + 'x' + $fh +
         ' (client ' + [int]$wr.Width + 'x' + [int]$wr.Height + ', frame ' + ($co.x - $gr.left) + ',' +
         ($co.y - $gr.top) + ', scale ' + $scale + ') kept at ' + $winShot)

    # The tab strip cropped out of that picture, in window-local coordinates. A few pixels of slack on
    # every side and room past the name: the button sits at the tab's trailing edge, and a crop of the
    # text alone would cut it off and read as "no button".
    $left = [int][math]::Floor($r.X - $gr.left - 4)
    $top = [int][math]::Floor($r.Y - $gr.top - 3)
    $width = [int][math]::Ceiling($r.Width + 14)
    $height = [int][math]::Ceiling($r.Height + 6)
    if ($left -lt 0) { $left = 0 }
    if ($top -lt 0) { $top = 0 }
    if ($left + $width -gt $fw) { $width = $fw - $left }
    if ($top + $height -gt $fh) { $height = $fh - $top }
    # The slack must not buy a ticket onto the frame, so clamp the crop into the client area first and
    # then refuse if the tab no longer fits inside it. This is asserted geometrically rather than by
    # colour on purpose: sec.91 first tried "a column of near-black means the frame" and it refused a
    # legitimately dark sidebar -- a claim about the user's theme, not about the window. And the
    # pre-fix crop is the case this catches: it covered bitmap rows 81-121 while the tab itself sits
    # at 115-150, so most of what it printed was the toolbar above the strip.
    $cl = $co.x - $gr.left
    $ct = $co.y - $gr.top
    if ($left -lt $cl) { $left = $cl }
    if ($top -lt $ct) { $top = $ct }
    if ($left + $width -gt $fw) { $width = $fw - $left }
    if ($top + $height -gt $fh) { $height = $fh - $top }
    $tabX = [int]($r.X - $gr.left)
    $tabY = [int]($r.Y - $gr.top)
    if ($tabX -lt $left -or $tabY -lt $top -or
        ($tabX + [int]$r.Width) -gt ($left + $width) -or
        ($tabY + [int]$r.Height) -gt ($top + $height)) {
      $winBmp.Dispose()
      Bail ('  the tab rect maps to bitmap ' + $tabX + ',' + $tabY + ' ' + [int]$r.Width + 'x' +
            [int]$r.Height + ' but the crop is ' + $left + ',' + $top + ' ' + $width + 'x' + $height +
            ' -- the tab is not inside this picture, so the column numbers would be about something ' +
            'else')
    }
    Say ('  tab maps to bitmap ' + $tabX + ',' + $tabY + ' ' + [int]$r.Width + 'x' + [int]$r.Height +
         '; crop ' + $left + ',' + $top + ' ' + $width + 'x' + $height)
    $cropRect = New-Object System.Drawing.Rectangle($left, $top, $width, $height)
    $bmp = $winBmp.Clone($cropRect, $winBmp.PixelFormat)
    $shot = Join-Path $scratch ('tabstrip-' + [System.IO.Path]::GetFileNameWithoutExtension($Doc) + '.png')
    $bmp.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
    # Ink per column in the trailing quarter, so "there is a glyph at the right end" is a number
    # beside the picture rather than only a look -- and a crop that missed the button says so here.
    $from = [int]($width * 0.72)
    for ($x = $from; $x -lt $width; $x += 4) {
      $ink = 0
      for ($y = 0; $y -lt $height; $y++) {
        $c = $bmp.GetPixel($x, $y)
        if ($c.R -lt 200 -or $c.G -lt 200 -or $c.B -lt 200) { $ink++ }
      }
      if ($ink -gt 0) { Say ('  column ' + ($x - $from) + ' ink=' + $ink) }
    }
    $bmp.Dispose(); $winBmp.Dispose()
    Say ('  crop ' + $width + 'x' + $height + ' at window-local ' + $left + ',' + $top +
         ' kept at ' + $shot)
  }
  'deletesummary' {
    # sec.77's rule, said in the delete box, on a screen, for the first time. That slice read the SPLIT
    # box on screen and left this one to the headless pair (positive and negative both pinned in
    # documenttab_smoke) -- so the sentence never had an eye on it in either language, and a
    # before-the-press warning nobody has seen is a claim about a string, not about a window.
    # Two documents, two verdicts: one carrying a field (the sentence must be there) and one without
    # it (it must not be, or the box warns about form fields on every file this program opens).
    # Nothing is pressed and nothing is saved; the run ends by killing the process.
    $docMenu = (CjStr 0x6587, 0x6863)                    # the Document menu, Chinese interface
    $delRow = (CjStr 0x5220, 0x9664, 0x9875, 0x9762)     # Delete Pages
    $formNeedle = (CjStr 0x8868, 0x5355, 0x5B57, 0x6BB5) # "form fields", in the Chinese form

    $win = $null
    for ($k = 1; $k -le 10 -and -not $win; $k++) {
      Start-Sleep -Milliseconds 500
      $win = Window $app.Id
    }
    if (-not $win) { Bail 'no document window' }
    # The foreground route sec.84 measured: a click sent while another window owns the screen lands
    # there instead, and the row still reports enabled=True.
    [W32]::SwitchToThisWindow([IntPtr]$win.Current.NativeWindowHandle, $true)
    Start-Sleep -Milliseconds 500
    $wr = $win.Current.BoundingRectangle
    [W32]::Click([int]($wr.X + $wr.Width / 2), [int]($wr.Y + 12))
    Start-Sleep -Milliseconds 800
    Say ('  document [' + $win.Current.Name + ']')

    TopMenu $app.Id @('Document', $docMenu) | Out-Null
    $row = RowAny $app.Id @('Delete Pages', $delRow)
    if (-not $row) { Bail 'no delete-pages row in either language' }
    Say ('  menu row |' + $row.Current.Name + '| enabled=' + $row.Current.IsEnabled)
    ClickAt $row
    Start-Sleep -Milliseconds 1500
    # Ours by CLASS: a Qt dialog reports its widget class name, which no translation can change,
    # while its title is one of our own translatable strings (sec.45). Polled rather than read once,
    # and every candidate printed when it fails: the first version looked for the class exactly once
    # after 1.5 s, printed "did not open", and left the question of whether the dialog was slow, was
    # elsewhere in the tree, or never came at all -- three different answers with one line between them.
    $dlg = $null
    for ($k = 1; $k -le 8 -and -not $dlg; $k++) {
      Start-Sleep -Milliseconds 400
      $cands = @()
      $main = Window $app.Id
      if ($main) { $cands += @(FindType $main $CT::Window) }
      $subCount = $cands.Count
      foreach ($w in @($AE::RootElement.FindAll($TS::Children, (New-Object $PC($AE::ProcessIdProperty, $app.Id))))) {
        $cands += $w
      }
      for ($c = 0; $c -lt $cands.Count; $c++) {
        $w = $cands[$c]
        if ($w.Current.ClassName -eq 'DeletePagesDialog') {
          $dlg = $w
          # Which round it took and where it was, because two things changed at once when this block
          # was written -- the wait became a poll and the scope grew to the process's own top-level
          # windows -- and a reading that cannot say which one mattered cannot be reused by the next
          # mode. The answer this slice got: round 1, and inside the main window's subtree.
          Say ('  DeletePagesDialog found on round ' + $k + ' (' + $cands.Count +
               ' candidates; it was ' +
               $(if ($c -lt $subCount) { 'in the main window subtree' } else { 'a top-level window of the process' }) + ')')
        }
      }
      if (-not $dlg -and $k -le 2) {
        Say ('  round ' + $k + ': ' + $cands.Count + ' candidate window(s), no DeletePagesDialog yet')
      }
    }
    if (-not $dlg) {
      foreach ($w in $cands) {
        Say ('  candidate [' + $w.Current.Name + '] class=' + $w.Current.ClassName)
      }
      Bail 'the delete-pages dialog did not open (candidates printed above)'
    }

    $texts = @(FindType $dlg $CT::Text)
    $longest = ''
    foreach ($t in $texts) { if ($t.Current.Name.Length -gt $longest.Length) { $longest = $t.Current.Name } }
    Say ('  summary (longest of ' + $texts.Count + ' Text): |' +
         $longest.Replace("`r", ' ').Replace("`n", ' / ') + '|')
    # Needled both ways for sec.42's reason, and read the lesson sec.84 wrote: a needle written for
    # two locales but only ever run in one has not been tested. These two documents are run in both.
    $has = ($longest -like ('*' + $formNeedle + '*')) -or ($longest -like '*form fields*')
    Say ('  form sentence on screen=' + $has + '  expected=' + $ExpectForm)
    if ($ExpectForm -eq 'present' -and -not $has) {
      Bail 'a document that carries a field opened the box without the form sentence'
    }
    if ($ExpectForm -eq 'absent' -and $has) {
      Bail 'the form sentence appeared on a document with no field at all'
    }
    foreach ($b in @(FindType $dlg $CT::Button)) {
      Say ('    button |' + $b.Current.Name + '| enabled=' + $b.Current.IsEnabled)
    }
  }
  'bookdialog' {
    # The bookmark editor, opened the way a reader opens it. Nothing else in this harness has ever
    # gone through menu -> dialog -> setOutline, because dialog.exec() blocks the caller and no test
    # can press a button that is inside a blocking call -- so the whole chain was unexercised when the
    # headless suite went green. Three things are read here that a headless check cannot: the UIA
    # shape of a QTableWidget (sec.65's QTreeView result says a control's shape is unknown until it
    # is measured -- totals from GridPattern, never from FindAll of what looked like rows), whether an
    # untouched box on a 184-bookmark document really arrives with its accept button greyed, and
    # which row the
    # Document menu shows once the request is recorded. Nothing is saved; the run ends by killing the
    # process, so a recorded tree never reaches anybody's file.
    $docMenu = (CjStr 0x6587, 0x6863)                # the Document menu, Chinese interface
    $edit = (CjStr 0x7F16, 0x8F91, 0x4E66, 0x7B7E)  # Edit Bookmarks / the dialog title
    $add = (CjStr 0x52A0, 0x4E00, 0x6761)          # Add one
    $remove = (CjStr 0x5220, 0x4E00, 0x6761)       # Remove one
    $up = (CjStr 0x4E0A, 0x79FB)                   # Move Up
    $down = (CjStr 0x4E0B, 0x79FB)                 # Move Down
    $write = (CjStr 0x5199, 0x8FD9)                # Write these %n bookmarks
    $cancel = (CjStr 0x53D6, 0x6D88)               # Cancel
    $undoNeedle = (CjStr 0x64A4, 0x9500)           # Undo

    Say ('  window title before: [' + (Window $app.Id).Current.Name + ']')
    TopMenu $app.Id @('Document', $docMenu) | Out-Null
    $row = RowAny $app.Id @('Edit Bookmarks', $edit)
    if (-not $row) { Bail 'no bookmark row in either language' }
    Say ('  menu row |' + $row.Current.Name + '| enabled=' + $row.Current.IsEnabled)
    ClickAt $row
    Start-Sleep -Milliseconds 1500
    $dlg = WaitDialog $app.Id @('Edit Bookmarks', $edit)
    if (-not $dlg) { Bail 'the bookmark dialog did not open' }
    $r = $dlg.Current.BoundingRectangle
    Say ('  dialog [' + $dlg.Current.Name + '] at ' + [int]$r.X + ',' + [int]$r.Y + ' ' +
         [int]$r.Width + 'x' + [int]$r.Height)

    # The table's shape, printed before any count is taken from it.
    $tables = @(FindType $dlg $CT::Table)
    Say ('  ' + $tables.Count + ' element(s) of ControlType.Table')
    foreach ($t in $tables) {
      $g = Pat $t 'Grid'
      if ($g) {
        Say ('  GridPattern rows=' + $g.Current.RowCount + ' columns=' + $g.Current.ColumnCount)
      } else {
        Say '  the table hands no GridPattern -- a row total cannot be read from this control'
      }
    }
    $all = @($dlg.FindAll($TS::Descendants, $ANY))
    foreach ($kind in @('DataItem', 'Custom', 'Text', 'Edit', 'Spinner', 'Button', 'Header', 'HeaderItem')) {
      # Counted off the descendant list rather than with a ControlType built by name: the first
      # version of this line asked ControlType for each kind and reported zero of every one of them,
      # including the DataItems and Buttons sitting plainly in the dump below it. A channel that
      # cannot find a thing reads exactly like a window that has no such thing (sec.58, sec.65).
      $hits = @($all | Where-Object { $_.Current.ControlType.ProgrammaticName -eq ('ControlType.' + $kind) })
      Say ('  ' + $kind + ': ' + $hits.Count)
    }
    Say ('  ' + $all.Count + ' descendants in total; the first 20 as type |name| patterns:')
    foreach ($e in @($all | Select-Object -First 20)) {
      $pats = @($e.GetSupportedPatterns() | ForEach-Object { $_.ProgrammaticName.Replace('Pattern', '') })
      $n = $e.Current.Name
      if ($n.Length -gt 46) { $n = $n.Substring(0, 46) + '...' }
      Say ('    ' + $e.Current.ControlType.ProgrammaticName + ' |' + $n.Replace("`r", ' ').Replace("`n", ' / ') +
           '| ' + ($pats -join ','))
    }

    $btns = @(FindType $dlg $CT::Button)
    Say ('  buttons (' + $btns.Count + '):')
    foreach ($b in $btns) {
      Say ('    |' + $b.Current.Name + '| enabled=' + $b.Current.IsEnabled)
    }

    # Cell by cell, which is the reading sec.74 exists for. A QTableWidget hands UIA one DataItem per
    # cell and only for the viewport, so three consecutive names are one row: title, level, page. sec.72
    # measured the middle of every triple as empty -- the level was a widget inside the cell, and a cell
    # widget does not appear in the tree at all -- so the numbers that follow are the whole question:
    # if the middle still reads empty, the column is still silent to a screen reader and the fix is a
    # comment. The empty total cannot be read as a pass or fail on its own, because a bookmark with no
    # page has a genuinely blank page cell.
    $items = @($all | Where-Object { $_.Current.ControlType.ProgrammaticName -eq 'ControlType.DataItem' })
    $blankCells = @($items | Where-Object { $_.Current.Name -eq '' }).Count
    $digitCells = @($items | Where-Object { $_.Current.Name -match '^[0-9]{1,4}$' }).Count
    Say ('  cells: ' + $items.Count + ' DataItem, ' + $blankCells + ' with an empty Name, ' +
         $digitCells + ' whose Name is just a number')
    for ($i = 0; $i + 2 -lt [math]::Min(12, $items.Count); $i += 3) {
      $triple = @()
      foreach ($c in @($items[$i], $items[$i + 1], $items[$i + 2])) {
        $cn = ''
        if ($c) { $cn = $c.Current.Name }
        if ($cn.Length -gt 20) { $cn = $cn.Substring(0, 20) + '...' }
        $triple += ('|' + $cn + '|')
      }
      Say ('    row ' + [int]($i / 3 + 1) + ': title ' + $triple[0] + ' level ' + $triple[1] +
           ' page ' + $triple[2])
    }
    $texts = @(FindType $dlg $CT::Text)
    $longest = ''
    foreach ($t in $texts) { if ($t.Current.Name.Length -gt $longest.Length) { $longest = $t.Current.Name } }
    Say ('  summary (longest of ' + $texts.Count + ' Text): |' +
         $longest.Replace("`r", ' ').Replace("`n", ' / ') + '|')

    # The pixels, because a count cannot see whether 184 rows fit, whether the level column reads as
    # a number to an eye, or whether the empty page cells look like anything at all.
    $hwnd = [IntPtr]$dlg.Current.NativeWindowHandle
    [W32]::SwitchToThisWindow($hwnd, $true)
    Start-Sleep -Milliseconds 600
    $scale = [W32]::GetDpiForWindow($hwnd) / 96.0
    # The bitmap has to be the window's OUTER size. UIA's BoundingRectangle for a Qt top-level is its
    # client area, while PrintWindow draws the whole window including the title bar -- framing the shot
    # at the client size cut the accept button's row off the bottom of the picture, which reads exactly
    # like a dialog whose layout does not fit. The rect line below is what tells the two apart.
    $outer = [W32]::Rect($hwnd)
    $fw = [int][math]::Ceiling(($outer.Right - $outer.Left) * $scale)
    $fh = [int][math]::Ceiling(($outer.Bottom - $outer.Top) * $scale)
    if ($fw -lt $r.Width -or $fh -lt $r.Height) {
      $fw = [int][math]::Ceiling($r.Width * $scale)
      $fh = [int][math]::Ceiling($r.Height * $scale)
    }
    $bmp = New-Object System.Drawing.Bitmap($fw, $fh)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $painted = [W32]::PrintWindow($hwnd, $hdc, 2)
    $g.ReleaseHdc($hdc)
    $shot = Join-Path $scratch ('bookdialog-' + [System.IO.Path]::GetFileNameWithoutExtension($Doc) + '.png')
    $bmp.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Say ('  PrintWindow returned ' + $painted + ', shot ' + $fw + 'x' + $fh +
         ' (UIA client ' + [int]$r.Width + 'x' + [int]$r.Height + ') kept at ' + $shot)

    # Untouched: the accept button must be off, and say why in the summary. This is the rule the
    # model cannot hold (it never reads the file's own /Outlines), so the screen is where it either
    # exists or does not.
    $go = MatchOne $dlg $CT::Button @('Write', $write)
    if (-not $go) { Bail 'no accept button found by either name' }
    Say ('  untouched accept |' + $go.Current.Name + '| enabled=' + $go.Current.IsEnabled)
    # Where the accept button actually sits. A picture that cuts its bottom edge cannot say which of
    # two things happened: the layout pushed the row below the window's own bottom (a product defect,
    # and the English summary is taller than the Chinese one), or the bitmap is short because UIA's
    # rectangle is the client area while PrintWindow draws the frame as well. Two rectangles answer it.
    $br = $go.Current.BoundingRectangle
    Say ('  accept rect ' + [int]$br.X + ',' + [int]$br.Y + ' ' + [int]$br.Width + 'x' + [int]$br.Height +
         ' ; dialog rect ' + [int]$r.X + ',' + [int]$r.Y + ' ' + [int]$r.Width + 'x' + [int]$r.Height +
         ' ; button bottom is ' + [int]([math]::Abs($br.Bottom - $r.Bottom)) + ' px ' +
         $(if ($br.Bottom -gt $r.Bottom) { 'BELOW' } else { 'inside' }) + ' the dialog edge')

    # One click on Add one, which is a real button press inside the blocking dialog -- the first
    # input this dialog has ever received from outside its own class.
    $addBtn = MatchOne $dlg $CT::Button @('Add one', $add)
    if (-not $addBtn) { Bail 'no Add one button' }
    ClickAt $addBtn
    Start-Sleep -Milliseconds 900
    $dlg = WaitDialog $app.Id @('Edit Bookmarks', $edit)
    $t2 = @(FindType $dlg $CT::Table)
    foreach ($t in $t2) {
      $g2 = Pat $t 'Grid'
      if ($g2) { Say ('  after Add one, GridPattern rows=' + $g2.Current.RowCount) }
    }
    $go = MatchOne $dlg $CT::Button @('Write', $write)
    Say ('  after Add one, accept |' + $go.Current.Name + '| enabled=' + $go.Current.IsEnabled)
    $texts2 = @(FindType $dlg $CT::Text)
    $longest2 = ''
    foreach ($t in $texts2) { if ($t.Current.Name.Length -gt $longest2.Length) { $longest2 = $t.Current.Name } }
    Say ('  and the summary: |' + $longest2.Replace("`r", ' ').Replace("`n", ' / ') + '|')

    # sec.80's sentence, on the same request the Add-one round just made. The box says, before the
    # press, how many of the file's own bookmarks name a place inside a page and that a save flattens
    # them -- and the count is taken off the file, not off this list, which is the whole reason the
    # screen has to be the one that reads it: the headless suites see a tree that has already lost
    # the difference. Needled in both languages for sec.42's reason (this harness starts in English,
    # and a Chinese-only needle reads "not there" while it is on the screen).
    # The needle as written could never have matched the Chinese form, and the first Chinese round
    # (sec.84, on the installed copy) is what caught it: the screen said "pointing at a place inside
    # the page" while
    # this asked for "the-page certain place" -- one character short of a substring, so it printed False over a
    # sentence that was plainly there. sec.80 recorded "both needles are accepted", which was only
    # ever tested through the English half, because this harness starts in English: a needle nobody
    # runs in the other language is decoration (sec.42's rule, and the reason it exists).
    $placeNeedle = (CjStr 0x7684, 0x67D0, 0x4E2A, 0x4F4D, 0x7F6E)  # "...certain place"
    $namesPlaces = ($longest2 -like ('*' + $placeNeedle + '*')) -or
                   ($longest2 -like '*a place inside a page*')
    Say ('  summary names the positions-at-risk sentence=' + $namesPlaces)

    # The refusal on screen, written through the cell's own Value pattern (sec.72's dump shows every
    # cell offers one). Two claims are tested here and only the screen can hold either: that a typed 0
    # stops the press, and that the cell can be written at all -- which is the thing a spin box inside
    # a cell could never be, since it was not in the tree. One attempt each way; if this channel does
    # not carry a write, that is the reading, not something to retry (sec.41).
    $levelNeedle = (CjStr 0x5C42, 0x7EA7)   # "level", the word the refusal sentence names
    $desc2 = @($dlg.FindAll($TS::Descendants, $ANY))
    $items2 = @($desc2 | Where-Object { $_.Current.ControlType.ProgrammaticName -eq 'ControlType.DataItem' })
    Say ('  cells after Add one: ' + $items2.Count)
    if ($items2.Count -ge 3) {
      Say ('    first row: |' + $items2[0].Current.Name + '| |' + $items2[1].Current.Name + '| |' +
           $items2[2].Current.Name + '|')
      $val = Pat $items2[1] 'Value'
      if (-not $val) {
        Say '  the second cell of the row hands no Value pattern -- nothing to type into'
      } else {
        Say ('    its Value=[' + $val.Current.Value + '] isReadOnly=' + $val.Current.IsReadOnly)
        try {
          $val.SetValue('0')
        } catch {
          Say ('  SetValue threw: ' + $_.Exception.Message)
        }
        Start-Sleep -Milliseconds 800
        $go = MatchOne $dlg $CT::Button @('Write', $write)
        $said = ''
        foreach ($t in @(FindType $dlg $CT::Text)) {
          if ($t.Current.Name.Length -gt $said.Length) { $said = $t.Current.Name }
        }
        # Matched in either language on purpose: this harness starts the app from its own scratch ini,
        # which says Language=en, so a Chinese-only needle reads "the sentence is not there" while the
        # sentence is on the screen in English -- the false negative sec.42 recorded for a different row.
        $namesLevel = ($said -like ('*' + $levelNeedle + '*')) -or ($said -like '*level*')
        Say ('  after typing 0 into that cell: accept enabled=' + $go.Current.IsEnabled +
             ', summary names the level column=' + $namesLevel)
        Say ('    summary: |' + $said.Replace("`r", ' ').Replace("`n", ' / ') + '|')
        try {
          $val.SetValue('1')
        } catch {
          Say ('  SetValue back threw: ' + $_.Exception.Message)
        }
        Start-Sleep -Milliseconds 800
        $go = MatchOne $dlg $CT::Button @('Write', $write)
        Say ('  after typing 1 back: accept enabled=' + $go.Current.IsEnabled)
      }
    }

    ClickAt $go
    Start-Sleep -Milliseconds 1200
    $still = WaitDialog $app.Id @('Edit Bookmarks', $edit) 3
    if ($still) { Say '  the dialog is still up after the accept click -- the request was not recorded' }
    Say ('  window title after: [' + (Window $app.Id).Current.Name + ']')

    # Every row of the Document menu, rather than a grep for the sentence expected: a needle list
    # that omits a row reads exactly like a row that is not there.
    TopMenu $app.Id @('Document', $docMenu) | Out-Null
    Start-Sleep -Milliseconds 500
    $rows = @(MenuItems $app.Id)
    Say ('  Document menu now has ' + $rows.Count + ' rows:')
    foreach ($e in $rows) {
      Say ('    |' + $e.Current.Name + '| enabled=' + $e.Current.IsEnabled)
    }
    $undoRow = $null
    foreach ($e in $rows) {
      if ($e.Current.Name -like ('*' + $undoNeedle + '*') -or $e.Current.Name -like 'Undo*') { $undoRow = $e; break }
    }
    if ($undoRow) {
      ClickAt $undoRow
      Start-Sleep -Milliseconds 900
      Say ('  after clicking it, window title: [' + (Window $app.Id).Current.Name + ']')
      TopMenu $app.Id @('Document', $docMenu) | Out-Null
      Start-Sleep -Milliseconds 500
      foreach ($e in @(MenuItems $app.Id)) {
        if ($e.Current.Name -like ('*' + $undoNeedle + '*') -or $e.Current.Name -like 'Undo*' `
            -or $e.Current.Name -like 'Redo*') { Say ('    |' + $e.Current.Name + '| enabled=' + $e.Current.IsEnabled) }
      }
    } else {
      Say '  no undo row at all in the Document menu'
    }
  }
  'propsdialog' {
    # The properties window (PLAN M2, Ctrl+D, class PropertiesDialog) is one of the things sec.100-105
    # added and no real window has ever shown. The headless suite reads PropertiesDialog::Info; this
    # reads what a reader gets: the class of the window Ctrl+D opens, the file name it states, the
    # Pages label, and a standalone number beside it.
    # The expected page count is NOT written here. It is read from the main window's own "1 / 40"
    # status label, so the verdict compares two on-screen statements; a hard-coded 40 would pass on
    # the 40-page fixture even if the dialog were never read. -Control expects one page too many,
    # which keeps every other step identical and makes the page-count verdict fail on purpose.
    $win = TakeForeground $app.Id
    if (-not $win) { Bail '  could not take the foreground; refusing to send Ctrl+D into another window' }
    $pageCount = PageStatusCount $app.Id
    Say ('  status bar page count: ' + $pageCount)
    if ($pageCount -lt 1) { Bail '  the main window states no page count, so there is nothing to compare the dialog with' }
    $leaf = [System.IO.Path]::GetFileName($Doc)
    $wantPages = if ($Control) { $pageCount + 1 } else { $pageCount }
    $pagesWord = if ($Language -eq 'zh_CN') { CjStr 0x9875, 0x6570 } else { 'Pages' }
    Say ('  expect file [' + $leaf + '] and ' + $pagesWord + '=' + $wantPages +
         $(if ($Control) { '  (CONTROL: the dialog cannot show this)' } else { '' }))
    [System.Windows.Forms.SendKeys]::SendWait('^d')
    # The window can exist before its labels do: the first read after Ctrl+D returned a dialog
    # with zero Text descendants. Poll for the class AND its first label rather than binding to
    # the first handle that matches, which is the stale-handle shape sec.87 wrote down.
    $dlg = $null
    for ($k = 1; $k -le 20 -and -not $dlg; $k++) {
      Start-Sleep -Milliseconds 400
      $d = FindClass $app.Id 'PropertiesDialog'
      if ($d -and @(FindType $d $CT::Text).Count -gt 0) { $dlg = $d }
    }
    if (-not $dlg) { Bail '  Ctrl+D did not leave a PropertiesDialog with any labels' }
    Say ('  dialog [' + $dlg.Current.Name + '] class=' + $dlg.Current.ClassName)
    $texts = @(FindType $dlg $CT::Text | ForEach-Object { $_.Current.Name })
    Say ('  ' + $texts.Count + ' Text element(s):')
    foreach ($t in $texts) { Say ('    |' + $t.Replace("`r", ' ').Replace("`n", ' / ') + '|') }
    $sawName = @($texts | Where-Object { $_.Trim() -eq $leaf }).Count -gt 0
    $pagesLabel = @($texts | Where-Object { $_.Trim() -eq $pagesWord }).Count -gt 0
    $sawPages = @($texts | Where-Object {
      (($_.Trim() -replace ',', '') -replace '\s', '') -eq "$wantPages"
    }).Count -gt 0
    Say ('  file name on screen=' + $sawName + '  label [' + $pagesWord + ']=' + $pagesLabel +
         '  standalone ' + $wantPages + '=' + $sawPages)
    # Close before judging. The verdict is already in hand, and a window-modal box left open would
    # be inherited by whatever the process does next (and by the next probe round if the kill fails).
    [System.Windows.Forms.SendKeys]::SendWait('{ESC}')
    $closed = WaitClassGone $app.Id 'PropertiesDialog' 10
    Say ('  Escape closed it: ' + $closed)
    if (-not $sawName) {
      if ($Control) { Bail ('  CONTROL: the dialog did not name the open file [' + $leaf + ']') }
      Bail ('  the properties dialog does not name the open file [' + $leaf + ']')
    }
    if (-not $pagesLabel) { Bail ('  no [' + $pagesWord + '] label in either language') }
    if (-not $sawPages) {
      if ($Control) {
        Bail ('  CONTROL: the dialog showed no standalone ' + $wantPages +
              ' -- the page-count verdict is live (CONTROL exit 1)')
      }
      Bail ('  no standalone [' + $wantPages + '] in the dialog; the Pages row does not agree with the status bar')
    }
    Say '  properties dialog agrees with the status bar on the file and the page count'
  }
  'findwhole' {
    # The find bar's Whole words box (objectName findWholeBox) has only ever been driven through
    # Qt's own signals in the headless suite. Here it gets a real click. text.pdf contains XYZPDF
    # twice and no standalone PDF, so the count must go from two hits to none -- a verdict that is
    # impossible to produce without actually setting the box.
    # Clicked, not TogglePattern.Toggle()'d: the pattern flipped the state but the second call did
    # not put the count back (measured), so it is not the route a reader takes.
    # -Control clicks Match case instead. That is a real click on a real box, so the reading is
    # exercised, but it cannot change this count; the whole-word state and the count both stay
    # wrong for the verdict, which must exit 1.
    $win = TakeForeground $app.Id
    if (-not $win) { Bail '  could not take the foreground' }
    # Ctrl+F first: it is the reader's own shortcut and it cannot miss a menu row. The menu route
    # is kept as the retry, because the one round written from the scratch notes clicked Find and
    # found no bar 700 ms later -- either the click did not land or the bar was slower than the
    # fixed sleep, and neither is a product finding. So: shortcut, poll for the four named fields,
    # then menu, then poll again.
    function Grab-FindBar {
      $f = FindAuto $win 'findField'
      $w = FindAuto $win 'findWholeBox'
      $c = FindAuto $win 'findCaseBox'
      $n = FindAuto $win 'findCount'
      if ($f -and $w -and $c -and $n) { return @($f, $w, $c, $n) }
      return $null
    }
    $parts = $null
    [System.Windows.Forms.SendKeys]::SendWait('^f')
    for ($k = 1; $k -le 12 -and -not $parts; $k++) {
      Start-Sleep -Milliseconds 300
      $parts = Grab-FindBar
    }
    if (-not $parts) {
      Say '  Ctrl+F did not show the bar within 3.6 s; trying the Edit menu'
      TopMenu $app.Id @('Edit', (CjStr 0x7F16, 0x8F91)) | Out-Null
      $row = RowAny $app.Id @('Find', (CjStr 0x67E5, 0x627E))
      if (-not $row) { Bail '  no Find row under Edit in either language' }
      Say ('  menu row |' + $row.Current.Name + '| enabled=' + $row.Current.IsEnabled)
      ClickAt $row
      for ($k = 1; $k -le 12 -and -not $parts; $k++) {
        Start-Sleep -Milliseconds 300
        $parts = Grab-FindBar
      }
    }
    if (-not $parts) {
      Say '  the find bar did not appear; the fields this window does expose:'
      foreach ($e in @(FindType $win $CT::Edit) + @(FindType $win $CT::CheckBox)) {
        Say ('    ' + $e.Current.ControlType.ProgrammaticName + ' |' + $e.Current.Name +
             '| id=[' + $e.Current.AutomationId + ']')
      }
      Bail '  no findField / findWholeBox / findCaseBox / findCount in either route'
    }
    $field = $parts[0]
    $whole = $parts[1]
    $case = $parts[2]
    $count = $parts[3]
    Say '  find bar up with all four named fields'
    $vp = Pat $field 'Value'
    if (-not $vp) { Bail '  the find field offers no Value pattern' }
    $vp.SetValue('PDF')
    $wantBefore = if ($Language -eq 'zh_CN') { (CjStr 0x7B2C) + ' 1 / 2 ' + (CjStr 0x4E2A) } else { '1 of 2' }
    $noMatch = if ($Language -eq 'zh_CN') { CjStr 0x6CA1, 0x6709, 0x627E, 0x5230 } else { 'No matches' }
    $gotBefore = WaitName $count $wantBefore 24
    Say ('  after typing PDF: count |' + $count.Current.Name + '| (wanted [' + $wantBefore + ']) -> ' + $gotBefore)
    if (-not $gotBefore) {
      Bail ('  the count never reached [' + $wantBefore + ']; this mode is written for ' +
            'tests/fixtures/text.pdf, whose two XYZPDF words are the two substring hits')
    }
    # Re-grab the boxes after the search ran. The elements found the instant the bar appeared
    # can still carry an empty rectangle -- one round printed 0,0 0x0 while the bar was plainly
    # on screen and the same ids measure 106x20 a second later -- so wait for a real one.
    # The local is NOT called $target: script parameters are case-insensitive, and -Target is
    # [string], so $target = <element> is silently coerced to the element's type name.
    $box = if ($Control) { $case } else { $whole }
    for ($k = 1; $k -le 10 -and $box.Current.BoundingRectangle.Width -le 0; $k++) {
      Start-Sleep -Milliseconds 200
      $fresh = Grab-FindBar
      if ($fresh) {
        $field = $fresh[0]; $whole = $fresh[1]; $case = $fresh[2]; $count = $fresh[3]
        $box = if ($Control) { $case } else { $whole }
      }
    }
    $tr = $box.Current.BoundingRectangle
    Say ('  clicking ' + $(if ($Control) { 'Match case (CONTROL)' } else { 'Whole words' }) +
         ' at ' + [int]$tr.X + ',' + [int]$tr.Y + ' ' + [int]$tr.Width + 'x' + [int]$tr.Height)
    if ($tr.Width -le 0) { Bail '  the Whole words box has no rectangle on screen; nothing was clicked' }
    ClickAt $box
    Start-Sleep -Milliseconds 500
    $wholeState = (Pat $whole 'Toggle').Current.ToggleState.ToString()
    $caseState = (Pat $case 'Toggle').Current.ToggleState.ToString()
    Say ('  after the click: whole=' + $wholeState + ' case=' + $caseState +
         ' count |' + $count.Current.Name + '|')
    if ($Control) {
      if ($wholeState -ne 'On') {
        Bail ('  CONTROL: clicking Match case left Whole words ' + $wholeState + ' and the count at |' +
              $count.Current.Name + '| -- the whole-word verdict is live (CONTROL exit 1)')
      }
      if ($count.Current.Name -eq $noMatch) {
        Bail '  CONTROL: the count reached No matches anyway, so the click was not what changed it'
      }
      Bail '  CONTROL: the control run did not redden the whole-word verdict'
    }
    if ($wholeState -ne 'On') { Bail ('  the Whole words box reads ' + $wholeState + ' after a real click') }
    $gotAfter = WaitName $count $noMatch 24
    Say ('  count after Whole words: |' + $count.Current.Name + '| (wanted [' + $noMatch + ']) -> ' + $gotAfter)
    if (-not $gotAfter) { Bail '  Whole words did not remove the two substring hits' }
    Say '  whole-word box: two substring hits became none, on screen'
  }
  'snapshot' {
    # The Snapshot tool (Ctrl+Shift+5) had never been run in a real window: the clipboard is not a
    # channel any headless test can read, so nothing had checked that a drag puts the page's pixels
    # on it. This mode sets a sentinel text first (so "the clipboard changed" is measurable), presses
    # the shortcut, drags across picture.pdf, and requires three readings to agree:
    #   * the toolbar's Snapshot toggle is On (the shortcut reached the action);
    #   * the status line names W x H (the app's own receipt of the crop);
    #   * the clipboard image has exactly that size and carries the page's red block.
    # -Control skips the shortcut; the toggle must then be Off and the mode exits 1.
    # NOTE: this overwrites the user's clipboard and cannot restore it -- PowerShell can read a
    # bitmap but there is no general "save every format and put it back" route.
    $win = TakeForeground $app.Id
    if (-not $win) { Bail '  could not take the foreground; the drag would land in another window' }
    try {
      [System.Windows.Forms.Clipboard]::SetText('XYZPDF-PROBE-SENTINEL')
    } catch {
      Bail ('  could not set the clipboard sentinel: ' + $_.Exception.Message)
    }
    Say ('  clipboard sentinel set; has text=' + [System.Windows.Forms.Clipboard]::ContainsText())
    if ($Control) {
      Say '  CONTROL: the Ctrl+Shift+5 press is skipped'
    } else {
      [System.Windows.Forms.SendKeys]::SendWait('^+5')
      Start-Sleep -Milliseconds 700
    }
    $snap = $null
    foreach ($e in @(FindType $win $CT::CheckBox)) {
      if ($e.Current.Name -eq 'Snapshot' -or $e.Current.Name -eq (CjStr 0x622A, 0x56FE)) { $snap = $e; break }
    }
    if (-not $snap) { Bail '  no Snapshot checkbox on the toolbar in either language' }
    $tp = Pat $snap 'Toggle'
    if (-not $tp) { Bail '  the Snapshot toolbar button has no TogglePattern' }
    $state = $tp.Current.ToggleState.ToString()
    Say ('  Snapshot toolbar toggle: ' + $state + ' (name |' + $snap.Current.Name + '|)')
    if ($state -ne 'On') {
      if ($Control) {
        Bail ('  CONTROL: with the shortcut skipped the Snapshot tool is ' + $state +
              ' -- the toolbar-state verdict is live (CONTROL exit 1)')
      }
      Bail ('  Ctrl+Shift+5 left the Snapshot tool ' + $state)
    }
    $pv = $null
    # By class name, not by control type: measured 2026-10-22, Qt exposes PageView under a type
    # that is none of Document/Pane/Image, and the scratch that first drove this tool matched the
    # class name over all descendants.
    foreach ($e in @($win.FindAll($TS::Descendants, $ANY))) {
      if ($e.Current.ClassName -eq 'PageView') { $pv = $e; break }
    }
    if (-not $pv) { Bail '  no PageView element to drag across' }
    $r = $pv.Current.BoundingRectangle
    $cx = [int]($r.X + $r.Width / 2)
    $cy = [int]($r.Y + $r.Height / 2)
    $dx = [int][math]::Min(100, [math]::Max(20, ($r.Width - 20) / 2))
    $dy = [int][math]::Min(75, [math]::Max(15, ($r.Height - 20) / 2))
    Say ('  PageView at ' + [int]$r.X + ',' + [int]$r.Y + ' ' + [int]$r.Width + 'x' + [int]$r.Height +
         '; dragging ' + (2 * $dx) + 'x' + (2 * $dy) + ' from ' + ($cx - $dx) + ',' + ($cy - $dy) +
         ' to ' + ($cx + $dx) + ',' + ($cy + $dy))
    [W32]::Drag($cx - $dx, $cy - $dy, $cx + $dx, $cy + $dy)
    # The message is shown for five seconds, so the poll is tight; the status bar's Name is where
    # showMessage() text lives (the StatusName comment explains why a Text search finds nothing).
    $enPat = '^Copied a ([0-9]+) . ([0-9]+) picture$'
    $zhPat = '^' + (CjStr 0x5DF2, 0x590D, 0x5236) + ' ([0-9]+) . ([0-9]+) ' +
             (CjStr 0x7684, 0x56FE, 0x7247) + '$'
    $msg = ''
    $mw = 0; $mh = 0
    for ($k = 1; $k -le 24; $k++) {
      Start-Sleep -Milliseconds 250
      $msg = StatusName $app.Id
      if ($msg -match $enPat) { $mw = [int]$Matches[1]; $mh = [int]$Matches[2]; break }
      if ($msg -match $zhPat) { $mw = [int]$Matches[1]; $mh = [int]$Matches[2]; break }
    }
    Say ('  status line [' + $msg + '] -> ' + $mw + ' x ' + $mh)
    if ($mw -lt 1 -or $mh -lt 1) { Bail '  the status line never said the picture was copied with a size' }
    $img = $null
    try { $img = [System.Windows.Forms.Clipboard]::GetImage() } catch {
      Say ('  GetImage threw: ' + $_.Exception.Message)
    }
    if (-not $img) { Bail '  no image on the clipboard after the drag' }
    $iw = $img.Width; $ih = $img.Height
    $red = 0
    for ($y = 0; $y -lt $ih; $y += 2) {
      for ($x = 0; $x -lt $iw; $x += 2) {
        $c = $img.GetPixel($x, $y)
        if ($c.R -gt 150 -and $c.G -lt 120 -and $c.B -lt 120) { $red++ }
      }
    }
    $img.Dispose()
    $text = ''
    try {
      if ([System.Windows.Forms.Clipboard]::ContainsText()) { $text = [System.Windows.Forms.Clipboard]::GetText() }
    } catch { }
    Say ('  clipboard image ' + $iw + 'x' + $ih + ', red samples ' + $red +
         ', sentinel still on the clipboard=' + ($text -eq 'XYZPDF-PROBE-SENTINEL'))
    if ($iw -ne $mw -or $ih -ne $mh) {
      Bail ('  the status line says ' + $mw + 'x' + $mh + ' but the clipboard image is ' + $iw + 'x' + $ih)
    }
    if ($text -eq 'XYZPDF-PROBE-SENTINEL') { Bail '  the clipboard still holds the sentinel text; no image was put there' }
    if ($red -lt 100) {
      Bail ('  the clipboard image carries only ' + $red + ' red sample(s); this mode is written for ' +
            'picture.pdf and its red block')
    }
    Say '  snapshot: the toolbar, the status line and the clipboard image agree, and the page red is in it'
  }
  'strippreview' {
    # The thumbnail strip's preview of unsaved edits has only been checked headlessly (the row model
    # and its tab text). This mode does it where a reader does: open the navigation sidebar, measure
    # the first thumbnail, rotate the current page through its dialog, then read the tab and the
    # thumbnail again.
    # What is measurable: the strip's tab text, and the red block in picture.pdf. The page itself is
    # white on white, so "the thumbnail is now landscape" cannot be read off its outline here; a
    # quarter turn maps the landscape red block to portrait, and that is the pixel half of the claim.
    # -Control skips the rotation but keeps both expectations, so the tab verdict must exit 1.
    $win = TakeForeground $app.Id
    if (-not $win) { Bail '  could not take the foreground' }
    # The sidebar row is checkable. Read it before clicking: on an already-open sidebar, a click
    # would HIDE it and the report would read "the strip never appeared". The pattern is read, not
    # called -- a TogglePattern.Toggle on a row of an open Qt menu does nothing (measured by the
    # outlinetree mode), so a real click is the only route.
    TopMenu $app.Id @('View', (CjStr 0x67E5, 0x770B)) | Out-Null
    $side = RowAny $app.Id @('Navigation Sidebar', (CjStr 0x5BFC, 0x822A, 0x4FA7, 0x680F))
    if (-not $side) { Bail '  no Navigation Sidebar row in either language' }
    $sideToggle = Pat $side 'Toggle'
    $sideState = '(no TogglePattern)'
    if ($sideToggle) { $sideState = $sideToggle.Current.ToggleState.ToString() }
    Say ('  sidebar row |' + $side.Current.Name + '| toggle=' + $sideState)
    if ($sideState -eq 'On') {
      [System.Windows.Forms.SendKeys]::SendWait('{ESC}')
      Start-Sleep -Milliseconds 400
    } else {
      ClickAt $side
      Start-Sleep -Milliseconds 1400
    }
    $stripTab = TabNamed $win @('Thumbnails', (CjStr 0x7F29, 0x7565, 0x56FE))
    if (-not $stripTab) {
      foreach ($t in @(FindType $win $CT::TabItem)) { Say ('  tab |' + $t.Current.Name + '|') }
      Bail '  the sidebar has no Thumbnails tab'
    }
    ClickAt $stripTab
    Start-Sleep -Milliseconds 800
    $plainTabName = $stripTab.Current.Name
    Say ('  strip tab |' + $plainTabName + '|')
    # The tiles are rendered off the UI thread. Retry the measurement: no row yet and a row that is
    # still being painted look the same in one reading.
    $plain = $null
    for ($k = 1; $k -le 12; $k++) {
      Start-Sleep -Milliseconds 500
      $plain = ThumbRed $win 'plain'
      if ($plain.ok -and $plain.red -gt 100) { break }
    }
    if (-not $plain.ok) { Bail ('  the plain thumbnail could not be measured: ' + $plain.why) }
    Say ('  plain thumbnail: red=' + $plain.red + ' bbox=' + $plain.bw + 'x' + $plain.bh +
         ' (row ' + $plain.row + ', scanned top ' + $plain.scanned + ' rows) saved ' + $plain.shot)
    if ($plain.red -le 100) { Bail '  the plain thumbnail has no red block; this mode is written for picture.pdf' }
    if ($plain.bw -le $plain.bh) {
      Bail ('  the plain red block is ' + $plain.bw + 'x' + $plain.bh + ', not landscape -- the ' +
            'thumbnail is not a picture of this page')
    }
    $previewNames = @('Thumbnails (preview of changes)',
                      (CjStr 0x7F29, 0x7565, 0x56FE, 0xFF08, 0x4FEE, 0x6539, 0x9884, 0x89C8, 0xFF09))
    if ($Control) {
      Say '  CONTROL: the Rotate Pages step is skipped'
    } else {
      TopMenu $app.Id @('Document', (CjStr 0x6587, 0x6863)) | Out-Null
      $rot = RowAny $app.Id @('Rotate Pages', (CjStr 0x65CB, 0x8F6C, 0x9875, 0x9762))
      if (-not $rot) { Bail '  no Rotate Pages row in either language' }
      Say ('  menu row |' + $rot.Current.Name + '| enabled=' + $rot.Current.IsEnabled)
      ClickAt $rot
      $dlg = WaitClass $app.Id 'RotatePagesDialog'
      if (-not $dlg) { Bail '  the rotate dialog did not open' }
      Say ('  dialog [' + $dlg.Current.Name + '] class=' + $dlg.Current.ClassName)
      $radio = $null
      foreach ($e in @(FindType $dlg $CT::RadioButton)) {
        if ($e.Current.Name -like '*Current page*' -or
            $e.Current.Name -like ('*' + (CjStr 0x5F53, 0x524D, 0x9875) + '*')) { $radio = $e; break }
      }
      if (-not $radio) { Bail '  no Current page radio in either language' }
      Say ('  clicking |' + $radio.Current.Name + '|')
      ClickAt $radio
      Start-Sleep -Milliseconds 400
      $go = $null
      foreach ($e in @(FindType $dlg $CT::Button)) {
        if ($e.Current.Name -eq 'Rotate these pages' -or
            $e.Current.Name -eq (CjStr 0x65CB, 0x8F6C, 0x8FD9, 0x4E9B, 0x9875)) { $go = $e; break }
      }
      if (-not $go) { Bail '  no Rotate these pages button' }
      Say ('  pressing |' + $go.Current.Name + '| enabled=' + $go.Current.IsEnabled)
      ClickAt $go
      if (-not (WaitClassGone $app.Id 'RotatePagesDialog' 20)) {
        Bail '  the rotate dialog did not close after the press'
      }
      Say '  rotate dialog closed'
    }
    # Both runs now hold the SAME expectation. That is what makes -Control a control: it skips the
    # action, not the verdict.
    $preview = $null
    for ($k = 1; $k -le 24; $k++) {
      Start-Sleep -Milliseconds 500
      $preview = TabNamed $win $previewNames
      if ($preview) { break }
    }
    if (-not $preview) {
      if ($Control) {
        Bail ('  CONTROL: with the rotation skipped the strip tab is still |' + $plainTabName +
              '| -- the preview-tab verdict is live (CONTROL exit 1)')
      }
      Bail ('  the strip tab never became |' + ($previewNames -join ' / ') + '|')
    }
    Say ('  strip tab now |' + $preview.Current.Name + '|')
    $after = $null
    for ($k = 1; $k -le 20; $k++) {
      $after = ThumbRed $win 'preview'
      if ($after.ok -and $after.red -gt 100 -and $after.bh -gt $after.bw) { break }
      Start-Sleep -Milliseconds 500
    }
    if (-not $after.ok) { Bail ('  the preview thumbnail could not be measured: ' + $after.why) }
    Say ('  preview thumbnail: red=' + $after.red + ' bbox=' + $after.bw + 'x' + $after.bh +
         ' saved ' + $after.shot)
    if ($after.red -le 100) { Bail '  the preview thumbnail has no red block' }
    if ($after.bh -le $after.bw) {
      Bail ('  the red block did not turn portrait after the rotation (' + $after.bw + 'x' + $after.bh + ')')
    }
    Say ('  strip preview: the tab says so, and the landscape block became portrait -- a quarter turn ' +
         'of the page (the page outline itself is white on white and is not measured here)')
  }
  'mnemonics' {
    # Alt+letter had never been pressed. sec.98 rewrote the English forms to in-word mnemonics and
    # checked them as strings; whether the accelerators exist on a real window is a different claim.
    # The bar order is File, Edit, View, Document, Go, Window, Language, Help. The Window letter
    # differs (w in English, n in Chinese); Language has no mnemonic in either interface.
    # Assert by INDEX, not by name: the Chinese UIA names carry their own "(F)" text, so a name
    # needle would turn this into a translation test (sec.87's lesson about lookups).
    # -Control skips the Go press but keeps the expectation, so the mnemonic verdict must exit 1.
    $win = TakeForeground $app.Id
    if (-not $win) { Bail '  could not take the foreground; Alt keys would go to another window' }
    $firstItems = @(MenuBarItems $win)
    Say ('  menu bar has ' + $firstItems.Count + ' item(s):')
    for ($i = 0; $i -lt $firstItems.Count; $i++) { Say ('    ' + $i + ' |' + $firstItems[$i].Current.Name + '|') }
    if ($firstItems.Count -lt 8) { Bail '  the menu bar is shorter than the eight items this mode indexes' }
    $map = if ($Language -eq 'zh_CN') {
      @{ 0 = 'f'; 1 = 'e'; 2 = 'v'; 3 = 'd'; 4 = 'g'; 5 = 'n'; 7 = 'h' }
    } else {
      @{ 0 = 'f'; 1 = 'e'; 2 = 'v'; 3 = 'd'; 4 = 'g'; 5 = 'w'; 7 = 'h' }
    }
    $negatives = if ($Language -eq 'zh_CN') { @('w', 'l') } else { @('n', 'l') }
    $labels = @('File', 'Edit', 'View', 'Document', 'Go', 'Window', 'Language', 'Help')
    foreach ($index in @(0, 1, 2, 3, 4, 5, 7)) {
      $win = Window $app.Id
      if (-not $win) { Bail '  the window disappeared mid-run' }
      if (-not (ForegroundIs $win)) {
        Bail '  the app lost the foreground; refusing to send an Alt key into another window'
      }
      $items = @(MenuBarItems $win)
      $letter = $map[$index]
      $skip = $Control -and $index -eq 4
      if ($skip) {
        Say ('  Alt+' + $letter + ' (CONTROL: press skipped)')
      } else {
        [System.Windows.Forms.SendKeys]::SendWait('%' + $letter)
        Start-Sleep -Milliseconds 800
      }
      $expanded = @()
      for ($i = 0; $i -lt $items.Count; $i++) {
        $xp = Pat $items[$i] 'ExpandCollapse'
        if ($xp) {
          $st = $xp.Current.ExpandCollapseState.ToString()
          if ($st -eq 'Expanded' -or $st -eq 'PartiallyExpanded') { $expanded += $i }
        }
      }
      Say ('  Alt+' + $letter + ' -> expanded [' + ($expanded -join ',') + '] (wanted ' + $index + ')')
      if ($expanded.Count -ne 1 -or $expanded[0] -ne $index) {
        if ($skip) {
          Bail ('  CONTROL: without the Alt+' + $letter + ' press nothing expanded (expected ' + $index +
                ') -- the mnemonic verdict is live (CONTROL exit 1)')
        }
        Bail ('  Alt+' + $letter + ' did not expand only item ' + $index + ' (' + $labels[$index] + ')')
      }
      [System.Windows.Forms.SendKeys]::SendWait('{ESC}')
      Start-Sleep -Milliseconds 400
    }
    foreach ($letter in $negatives) {
      $win = Window $app.Id
      if (-not $win) { Bail '  the window disappeared mid-run' }
      if (-not (ForegroundIs $win)) {
        Bail '  the app lost the foreground; refusing to send an Alt key into another window'
      }
      $items = @(MenuBarItems $win)
      [System.Windows.Forms.SendKeys]::SendWait('%' + $letter)
      Start-Sleep -Milliseconds 800
      $expanded = @()
      for ($i = 0; $i -lt $items.Count; $i++) {
        $xp = Pat $items[$i] 'ExpandCollapse'
        if ($xp) {
          $st = $xp.Current.ExpandCollapseState.ToString()
          if ($st -eq 'Expanded' -or $st -eq 'PartiallyExpanded') { $expanded += $i }
        }
      }
      Say ('  Alt+' + $letter + ' (negative) -> expanded [' + ($expanded -join ',') + ']')
      if ($expanded.Count -ne 0) {
        Bail ('  Alt+' + $letter + ' expanded item(s) ' + ($expanded -join ',') +
              ', but that letter belongs to no menu in this interface')
      }
      [System.Windows.Forms.SendKeys]::SendWait('{ESC}')
      Start-Sleep -Milliseconds 300
    }
    Say '  every expected Alt letter opened exactly its own menu; the negatives opened nothing'
  }
  'translatesetup' {
    # handoff-03 F9: the box F1 puts up when no service is configured has never been on a screen, and
    # neither has the road out of it. The scratch store this script writes carries one key only
    # (appearance/language), so no provider is configured and the gate answers NotConfigured -- which
    # is also why this mode needs no credential of any kind: F1 reads a stored key only once a
    # provider is chosen, and the box that asks for a key at all is the NeedsConsent branch, left to
    # hand-running as F9 requires.
    # The text has to be REAL text: translateText() returns without a word when the selection is
    # empty, so Ctrl+A comes first (the Edit menu's own Select All) and then Ctrl+T. The notice
    # appearing is itself the proof that a selection existed -- with nothing selected nothing appears
    # at all -- and the two lines plus the Open Settings button are what say this is the box that
    # asks for a service, not the one that asks for consent (that one offers a Send button and only
    # exists once a key is stored, which this branch does not write).
    # Then the geometry: the settings dialog grew a whole group for these rows and nobody has looked
    # at it (sec.71 measured a box whose third column was cut off at 620 wide with every count green;
    # sec.110 found the find bar eating half a window the same way). So the dialog is printed with
    # PrintWindow AND judged by rectangles, and both the picture and the numbers go in the log.
    # -Control keeps every step and then looks for the settings dialog without pressing the button
    # that opens it. It cannot be there, so the control exits 1 by construction.
    $toSettingsCj = CjStr 0x53BB, 0x8BBE, 0x7F6E                     # the button, Chinese interface
    $needServiceCj = CjStr 0x9700, 0x8981, 0x4E00, 0x4E2A, 0x670D, 0x52A1  # main line, Chinese
    $enterKeyCj = CjStr 0x586B, 0x5199, 0x5BC6, 0x94A5               # informative line, Chinese

    $win = TakeForeground $app.Id
    if (-not $win) { Bail '  could not take the foreground; Ctrl+A and Ctrl+T would go to another window' }
    Say ('  document [' + $win.Current.Name + ']')
    [System.Windows.Forms.SendKeys]::SendWait('^a')
    Start-Sleep -Milliseconds 900
    [System.Windows.Forms.SendKeys]::SendWait('^t')
    # Poll for the class AND its first label: a window on its way in answers an empty Name for a
    # moment and then every later read is about a stale handle (sec.87). QMessageBox is the widget
    # class, so this holds whatever the title has been translated into.
    $box = $null
    for ($k = 1; $k -le 15 -and -not $box; $k++) {
      Start-Sleep -Milliseconds 400
      $b = FindClass $app.Id 'QMessageBox'
      if ($b -and @(FindType $b $CT::Text).Count -gt 0) { $box = $b }
    }
    if (-not $box) {
      PrintCandidates $app.Id 'no QMessageBox with any label; every candidate window of the process:'
      Bail '  Ctrl+A then Ctrl+T produced no setup notice (candidates printed above)'
    }
    Say ('  notice window [' + $box.Current.Name + '] class=' + $box.Current.ClassName)
    $texts = @(FindType $box $CT::Text)
    Say ('  ' + $texts.Count + ' Text element(s):')
    $joined = ''
    foreach ($t in $texts) {
      $flat = $t.Current.Name.Replace("`r", ' ').Replace("`n", ' / ')
      Say ('    |' + $flat + '|')
      $joined += $flat + ' | '
    }
    $buttons = @(FindType $box $CT::Button)
    Say ('  ' + $buttons.Count + ' Button element(s):')
    foreach ($b in $buttons) { Say ('    |' + $b.Current.Name + '| enabled=' + $b.Current.IsEnabled) }
    $sawNeed = ($joined -like ('*' + $needServiceCj + '*')) -or ($joined -like '*needs an API key*')
    $sawRoute = ($joined -like ('*' + $enterKeyCj + '*')) -or ($joined -like '*Choose a translation service*')
    $toSettings = MatchOne $box $CT::Button @('Open Settings', $toSettingsCj)
    Say ('  main line=' + $sawNeed + '  settings route line=' + $sawRoute + '  button=' +
         $(if ($toSettings) { '|' + $toSettings.Current.Name + '|' } else { 'NONE' }))
    if (-not $sawNeed) { Bail '  the notice does not say that translating needs an API key' }
    if (-not $sawRoute) { Bail '  the notice does not say to choose a service and enter the key in Settings' }
    if (-not $toSettings) { Bail '  the notice has no settings button in either language' }
    if ($Control) {
      $decline = MatchOne $box $CT::Button @('Cancel', (CjStr 0x53D6, 0x6D88))
      if (-not $decline) { Bail '  CONTROL: the notice has no Cancel button to leave it by' }
      Say ('  CONTROL: pressing |' + $decline.Current.Name + '| and not the settings button')
      ClickAt $decline
      Start-Sleep -Milliseconds 1200
      if (FindClass $app.Id 'SettingsDialog') {
        Bail '  CONTROL: the settings dialog came up although no button was pressed -- the focus verdict is not about the notice'
      }
      Bail '  CONTROL: no settings dialog without the press; the road out of the notice is live (CONTROL exit 1)'
    }
    Say ('  pressing |' + $toSettings.Current.Name + '|')
    ClickAt $toSettings
    $dlg = WaitClass $app.Id 'SettingsDialog' 15
    if (-not $dlg) {
      PrintCandidates $app.Id 'no SettingsDialog; every candidate window of the process:'
      Bail '  the settings button did not open the settings dialog'
    }
    Start-Sleep -Milliseconds 600
    Say ('  settings dialog [' + $dlg.Current.Name + '] class=' + $dlg.Current.ClassName)
    # Where the caret is, read two ways. Every earlier mode in this file judges a Name, a toggle
    # state or a pixel and never the focus, so "the focus lands on the service row" had not been
    # measured here at all; the two channels are printed with the verdict so the reading names its
    # own source (sec.99: a control that cannot change the outcome looks like a check and is one).
    $byProperty = @()
    for ($k = 1; $k -le 10 -and -not $byProperty; $k++) {
      Start-Sleep -Milliseconds 300
      $dlg = WaitClass $app.Id 'SettingsDialog' 2
      if ($dlg) { $byProperty = @(@($dlg.FindAll($TS::Descendants, $ANY)) | Where-Object { $_.Current.HasKeyboardFocus }) }
    }
    $direct = $null
    try { $direct = $AE::FocusedElement } catch { Say ('  FocusedElement threw: ' + $_.Exception.Message) }
    Say ('  HasKeyboardFocus reports ' + $byProperty.Count + ' element(s)')
    foreach ($f in $byProperty) {
      Say ('    focused |' + $f.Current.Name + '| type=' + $f.Current.ControlType.ProgrammaticName +
           ' class=' + $f.Current.ClassName)
    }
    Say ('  FocusedElement says [' + $(if ($direct) { $direct.Current.Name + '] class=' + $direct.Current.ClassName } else { '(nothing)' }))
    Say '  every combo box in the dialog, focused or not (its UIA Name is the row label it sits on,'
    Say '  not the value it shows -- measured on the first run of this mode; the value is printed'
    Say '  beside it where the widget offers one, as a reading of the state this run starts in):'
    foreach ($c in @(FindType $dlg $CT::ComboBox)) {
      $shown = '(no Value pattern)'
      $cv = Pat $c 'Value'
      if ($cv) { try { $shown = '|' + $cv.Current.Value + '|' } catch { $shown = 'threw' } }
      Say ('    |' + $c.Current.Name + '| focused=' + $c.Current.HasKeyboardFocus + ' value=' + $shown)
    }
    if (-not $byProperty.Count) { Bail '  nothing in the settings dialog reports the keyboard focus' }
    # The service row, in either interface: the Chinese label is the four code points on the line
    # below (this file stays pure ASCII, so it is not written out here), and it is what the form
    # layout puts beside the box -- measured above is what the box is called on screen. Matching the
    # row rather than the value is also what makes this verdict survive a change of provider: the
    # label of the row the reader was sent to is the claim, not which item that row shows.
    $want = if ($Language -eq 'zh_CN') { (CjStr 0x7FFB, 0x8BD1, 0x670D, 0x52A1) } else { 'Translation service' }
    $onService = $false
    foreach ($f in $byProperty) {
      if ($f.Current.ControlType.ProgrammaticName -eq 'ControlType.ComboBox' -and $f.Current.Name.Trim() -eq $want) { $onService = $true }
    }
    Say ('  focus is on the translation-service row=' + $onService + ' (wanted a combo box named [' + $want + '])')
    if (-not $onService) { Bail '  the settings dialog opened with the focus somewhere other than the translation service' }

    # The picture, sized by the OUTER window rect and judged against the dialog's own UIA rectangle.
    # Two rectangles because they are two different things: UIA gives a Qt top-level as its client
    # area, while the bitmap PrintWindow fills is the whole window including frame and title bar
    # (sec.74 nearly read a button as pushed out of the window for exactly this reason). One of them
    # is for the eye, the other is the coordinate space the rows live in.
    $dh = [IntPtr]$dlg.Current.NativeWindowHandle
    $gr = [W32]::Rect($dh)
    $co = [W32]::ClientOrigin($dh)
    $rc = New-Object W32+RECT
    [W32]::GetClientRect($dh, [ref]$rc) | Out-Null
    $fw = [int]($gr.right - $gr.left)
    $fh = [int]($gr.bottom - $gr.top)
    $pic = New-Object System.Drawing.Bitmap($fw, $fh)
    $g = [System.Drawing.Graphics]::FromImage($pic)
    $hdc = $g.GetHdc()
    $painted = [W32]::PrintWindow($dh, $hdc, 2)
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    $leaf = [System.IO.Path]::GetFileNameWithoutExtension($Doc)
    $shot = Join-Path $scratch ('translatesetup-settings-' + $Language + '-' + $leaf + '.png')
    $pic.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
    $pic.Dispose()
    $dr = $dlg.Current.BoundingRectangle
    Say ('  PrintWindow returned ' + $painted + ', window ' + $fw + 'x' + $fh + ' device px (client ' +
         [int]$rc.right + 'x' + [int]$rc.bottom + ', frame offset ' + ($co.x - $gr.left) + ',' + ($co.y - $gr.top) +
         '); UIA client rect ' + [int]$dr.X + ',' + [int]$dr.Y + ' ' + [int]$dr.Width + 'x' + [int]$dr.Height +
         '; picture kept at ' + $shot)
    $rowTypes = @('ControlType.ComboBox', 'ControlType.Edit', 'ControlType.CheckBox', 'ControlType.Spinner',
                  'ControlType.Button', 'ControlType.Text', 'ControlType.Group')
    $measured = 0
    $offenders = @()
    foreach ($e in @($dlg.FindAll($TS::Descendants, $ANY))) {
      $ctn = $e.Current.ControlType.ProgrammaticName
      if ($rowTypes -notcontains $ctn) { continue }
      $measured++
      $r = $e.Current.BoundingRectangle
      $tag = $ctn.Replace('ControlType.', '')
      $name = $e.Current.Name.Replace("`r", ' ').Replace("`n", ' / ')
      if ($name.Length -gt 46) { $name = $name.Substring(0, 46) }
      Say ('    row ' + $tag + ' |' + $name + '| at ' + [int]$r.X + ',' + [int]$r.Y + ' ' +
           [int]$r.Width + 'x' + [int]$r.Height)
      if ($r.Width -le 0 -or $r.Height -le 0) {
        $offenders += ($tag + ' |' + $name + '| has no rectangle on screen')
        continue
      }
      if ($r.X -lt ($dr.X - 1) -or $r.Y -lt ($dr.Y - 1) -or
          ($r.X + $r.Width) -gt ($dr.X + $dr.Width + 1) -or
          ($r.Y + $r.Height) -gt ($dr.Y + $dr.Height + 1)) {
        $offenders += ($tag + ' |' + $name + '| at ' + [int]$r.X + ',' + [int]$r.Y + ' ' + [int]$r.Width +
                       'x' + [int]$r.Height + ' is outside the dialog client ' + [int]$dr.X + ',' +
                       [int]$dr.Y + ' ' + [int]$dr.Width + 'x' + [int]$dr.Height)
      }
    }
    # Cancel, never the accept button: reject() puts back what the dialog staged, accepted() would
    # write settings (and the provider row is the one thing this run must leave alone).
    ClickCancel $dlg 'SettingsDialog' $app.Id
    if ($offenders.Count) {
      foreach ($o in $offenders) { Say ('  OUTSIDE: ' + $o) }
      Bail ('  ' + $offenders.Count + ' of ' + $measured + ' rows are not inside the settings dialog -- look at ' + $shot)
    }
    Say ('  ' + $measured + ' rows measured, every one inside the dialog client area; ' + $shot)
  }
  'xfanotice' {
    # handoff-03 F9: the two status sentences F3 added, and the delete box line about the XFA copy,
    # on a screen. One document per run: the program is single-instance and forwards a second launch
    # to the first, so three documents are three runs and -ExpectXfa is what decides the verdict.
    #   packet   field-xfa.pdf    -- ordinary XFA form: the second-copy status line, and the box
    #                               carrying the packet sentence
    #   dynamic  xfa-dynamic.pdf -- the packet plus the catalog key that asks a reader to draw the
    #                               pages itself: the dynamic status line (the branch checked FIRST,
    #                               so this file must not be answered with the weaker sentence) and
    #                               the same box line
    #   absent   plain.pdf        -- no packet: neither status line and no box sentence, or the
    #                               program warns about XFA on every PDF it opens
    # The status line is never judged on the bare word XFA. That line names the file that opened, and
    # field-xfa.pdf would satisfy such a needle before any sentence existed -- sec.97's rule that a
    # receipt has to be impossible before the thing it certifies. In the delete box no file name is
    # printed, so XFA there is a fair needle.
    # The control is a combination rather than the -Control switch, exactly as deletesummary does it:
    # -ExpectXfa packet on plain.pdf asks for a sentence that file cannot produce, and that round must
    # exit 1.
    if ($ExpectXfa -ne 'packet' -and $ExpectXfa -ne 'dynamic' -and $ExpectXfa -ne 'absent') {
      Bail '  -ExpectXfa must be packet, dynamic or absent for this mode'
    }
    $leaf = [System.IO.Path]::GetFileName($Doc)
    Say ('  document [' + $leaf + ']  -ExpectXfa ' + $ExpectXfa)
    # showMessage(..., 12000) puts the line up and takes it away again, and the document is opened
    # during the launch wait, so read early and print EVERY round: an empty read would otherwise look
    # exactly like a sentence that never appears, and the round that started this list showed the
    # status bar carrying a toolbar hint instead (ParkCursorAway).
    $status = ''
    $seen = @()
    for ($k = 1; $k -le 15 -and ($status -notlike ('*' + $leaf + '*')); $k++) {
      $status = StatusName $app.Id
      if ($status -notin $seen) {
        $seen += $status
        Say ('  round ' + $k + ': |' + $status.Replace("`r", ' ').Replace("`n", ' / ') + '|')
      }
      Start-Sleep -Milliseconds 300
    }
    $flat = $status.Replace("`r", ' ').Replace("`n", ' / ')
    # The companion row every negative reading needs (sec.82): the line has to be about THIS document
    # before its silence about XFA means anything.
    if ($flat -notlike ('*' + $leaf + '*')) {
      Bail '  the status line never names the open document, so it is not an answer to anything'
    }
    $dynNeedles = @('dynamic XFA document', (CjStr 0x52A8, 0x6001))
    $packetNeedles = @('second copy of its form as XFA', (CjStr 0x8FD8, 0x6709, 0x4E00, 0x4EFD))
    $sawDyn = @($dynNeedles | Where-Object { $flat -like ('*' + $_ + '*') }).Count -gt 0
    $sawPacket = @($packetNeedles | Where-Object { $flat -like ('*' + $_ + '*') }).Count -gt 0
    Say ('  status line says packet=' + $sawPacket + ' dynamic=' + $sawDyn)
    if ($ExpectXfa -eq 'packet') {
      if (-not $sawPacket) { Bail ('  a document carrying an XFA packet opened without the second-copy sentence [' + $leaf + ']') }
      if ($sawDyn) { Bail '  the packet file was answered with the dynamic sentence -- the two kinds are not told apart' }
    } elseif ($ExpectXfa -eq 'dynamic') {
      if (-not $sawDyn) { Bail ('  a dynamic XFA document opened without the dynamic sentence [' + $leaf + ']') }
    } elseif ($sawPacket -or $sawDyn) {
      Bail ('  a document with no XFA at all opened with an XFA status sentence [' + $leaf + ']')
    }
    $dlg = OpenDocumentDialog $app.Id @('Delete Pages', (CjStr 0x5220, 0x9664, 0x9875, 0x9762)) 'DeletePagesDialog'
    # Read twice: once as the box opens, then once after a range is typed, because the summary is
    # recomputed on every keystroke and a sentence that only survives the untouched box would be a
    # sentence about a request nobody has made yet.
    $atOpen = LongestText $dlg
    Say ('  box at open (' + @(FindType $dlg $CT::Text).Count + ' Text): |' +
         $atOpen.Replace("`r", ' ').Replace("`n", ' / ') + '|')
    $edit = FieldOf $dlg
    RangeText $edit '2'
    $summary = LongestText $dlg
    Say ('  box with range 2: |' + $summary.Replace("`r", ' ').Replace("`n", ' / ') + '|')
    foreach ($b in @(FindType $dlg $CT::Button)) { Say ('    button |' + $b.Current.Name + '| enabled=' + $b.Current.IsEnabled) }
    $boxAtOpen = ($atOpen -like '*XFA*')
    $boxTyped = ($summary -like '*XFA*')
    $wantBox = ($ExpectXfa -ne 'absent')
    Say ('  box sentence at open=' + $boxAtOpen + ' after typing=' + $boxTyped + '  expected=' + $wantBox)
    if ($wantBox) {
      if (-not $boxAtOpen) { Bail ('  the delete box on a file carrying a packet never says the XFA copy goes [' + $leaf + ']') }
      if (-not $boxTyped) { Bail ('  the delete box stopped saying it once a range was typed [' + $leaf + ']') }
    } elseif ($boxAtOpen -or $boxTyped) {
      Bail '  the delete box says an XFA copy goes on a document that has none'
    }
    Say ('  status line and delete box both agree with the file [' + $leaf + ']')
  }
  'replynotice' {
    # handoff-03 F9: the before-the-press sentence sec.47 asked for, in the two boxes that can say it.
    # The document is the generated cross-page thread -- one /Text note on page 1, one reply naming it
    # with /IRT on page 2, and 38 more pages for the range box to point at. So four readings, none of
    # which presses an accept button:
    #   range 1  the note page goes           -> the box names one orphaned reply, and the number is 1
    #   range 3  neither end of the thread    -> the box says nothing
    #   split 1 page per piece                -> the two ends land in two files -> it names 1
    #   split 2 pages per piece               -> one piece holds both           -> it says nothing
    # The count is read OUT OF THE SENTENCE (ReplyCount), not from what this mode asked for: %n is
    # filled by Qt plural machinery and the catalogue half of that is pinned by an i18n_smoke probe,
    # so the screen has to show the number it shows rather than the number expected.
    # -Control keeps every step and expects the sentence at range 3, where it cannot be.
    $replyCj = CjStr 0x6761, 0x56DE, 0x7B54, 0x4F1A, 0x5931, 0x53BB   # the delete box sentence, Chinese
    $severCj = CjStr 0x88AB, 0x62C6, 0x5230, 0x4E24, 0x4E2A, 0x6587, 0x4EF6, 0x91CC, 0x53BB  # the split one
    $leaf = [System.IO.Path]::GetFileName($Doc)
    Say ('  document [' + $leaf + ']' + $(if ($Control) { '  (CONTROL: the sentence will be expected at range 3)' } else { '' }))
    $dlg = OpenDocumentDialog $app.Id @('Delete Pages', (CjStr 0x5220, 0x9664, 0x9875, 0x9762)) 'DeletePagesDialog'
    $edit = FieldOf $dlg
    RangeText $edit '1'
    $one = LongestText $dlg
    Say ('  range 1: |' + $one.Replace("`r", ' ').Replace("`n", ' / ') + '|')
    $sawOne = ($one -like ('*' + $replyCj + '*')) -or ($one -like '*reply loses the annotation*') -or ($one -like '*replies lose the annotation*')
    $nOne = ReplyCount $one
    Say ('  range 1: sentence=' + $sawOne + ' count=' + $nOne + '  (wanted the sentence and 1)')
    if (-not $sawOne -or $nOne -ne 1) {
      Bail ('  naming page 1 -- the note the reply answers -- does not put one orphaned reply in the box (sentence=' +
            $sawOne + ' count=' + $nOne + ')')
    }
    RangeText $edit '3'
    $three = LongestText $dlg
    Say ('  range 3: |' + $three.Replace("`r", ' ').Replace("`n", ' / ') + '|')
    $sawThree = ($three -like ('*' + $replyCj + '*')) -or ($three -like '*loses the annotation*')
    Say ('  range 3: sentence=' + $sawThree + '  expected=' + $(if ($Control) { 'present (CONTROL)' } else { 'absent' }))
    if ($Control) {
      if (-not $sawThree) {
        Bail '  CONTROL: page 3, which holds neither end of the thread, shows no orphan sentence -- the verdict is live (CONTROL exit 1)'
      }
      Bail '  CONTROL: the sentence appeared for page 3, so it is not conditional on the request at all'
    }
    if ($sawThree) { Bail '  the delete box warns about an orphaned reply for a page that holds neither end of the thread' }
    ClickCancel $dlg 'DeletePagesDialog' $app.Id
    $split = OpenDocumentDialog $app.Id @('Split Files', (CjStr 0x62C6, 0x5206, 0x6587, 0x4EF6)) 'SplitPagesDialog'
    $spin = @(@($split.FindAll($TS::Descendants, (New-Object $PC($AE::ControlTypeProperty, $CT::Spinner))))[0])
    $rv = Pat $spin 'RangeValue'
    if (-not $rv) { Bail '  the split box has no RangeValuePattern' }
    $rv.SetValue(1)
    Start-Sleep -Milliseconds 700
    $s1 = LongestText $split
    Say ('  split, 1 page per piece: |' + $s1.Replace("`r", ' ').Replace("`n", ' / ') + '|')
    $sawS1 = ($s1 -like ('*' + $severCj + '*')) -or ($s1 -like '*land in two different files*')
    $nS1 = ReplyCount $s1
    Say ('  split 1: sentence=' + $sawS1 + ' count=' + $nS1 + '  (wanted the sentence and 1)')
    if (-not $sawS1 -or $nS1 -ne 1) {
      Bail ('  splitting every page into its own file does not put one severed thread in the box (sentence=' +
            $sawS1 + ' count=' + $nS1 + ')')
    }
    $rv.SetValue(2)
    Start-Sleep -Milliseconds 700
    $s2 = LongestText $split
    Say ('  split, 2 pages per piece: |' + $s2.Replace("`r", ' ').Replace("`n", ' / ') + '|')
    $sawS2 = ($s2 -like ('*' + $severCj + '*')) -or ($s2 -like '*land in two different files*')
    Say ('  split 2: sentence=' + $sawS2 + '  expected=False (both ends are in one piece)')
    if ($sawS2) { Bail '  the split box warns about a severed thread when the note and its reply go to the same file' }
    ClickCancel $split 'SplitPagesDialog' $app.Id
    Say '  both boxes name the orphaned reply exactly for the requests that make one'
  }
}
$guardHeld = Guard-Check 'end of run'
Get-Process -Name xyzpdf -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
if (-not $guardHeld) {
  Say 'GUARD FAILED: a save mode wrote the file it was handed, not the copy it made'
  exit 1
}
Say 'DONE'
