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
    key sent to somebody else's window is not a reading. The three added on 2026-10-23
    (saveinplace, closeprompt, saveas) type chords the same way and with the same check -- and they
    are the only modes this file lets write anything, which is what the save guard below is for.

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
      saveas         Ctrl+Shift+S -> the native save box -> a full path written into its file field
                     -> a REAL click on its accept (BM_CLICK is honoured by the open dialog and the
                     folder picker but not by this one), then the receipts read back from the new
                     file, the caption, the status line and the two hashes that must not move.
                     (sec.44 measured the route; the mode's own comment says what F8 did to the row
                     this mode was written against. 'saverename' is kept as an alias for it.)
      saveinplace    the Ctrl+S F8 turned into a write, read four ways at once on a copy this probe
                     made: rotate page 1 through the dialog, watch the caption grow ' *', press the
                     key, and require the status line to name that copy, the star to go, the copy's
                     sha256 to move (and to carry /Rotate 90 afterwards), and the original's not to.
                     -Control keeps all four verdicts and presses nothing, so it exits 1.
      closeprompt    the three answers a close gives when the tab still holds unwritten edits, one
                     round each: Cancel keeps the window and writes nothing, Discard closes and
                     writes nothing, Save closes and writes. The box is found by the CLASS name
                     QMessageBox because every word in it is translated; its buttons are matched by
                     EXACT name, since 'Save' is a substring of "Don't Save" and this is the one box
                     where picking the neighbour writes a file. -Control closes a document that was
                     never edited and still expects the box, so it exits 1.
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
      formfill       fill one text field ON SCREEN and read the answer back out of the file: click
                     inside the field's own rectangle (aimed from the rectangle pdfprobe reads out of
                     the copy that is open, plus the white page found in a PrintWindow picture), type
                     a token made up for this run, require the caption's star, require the ink inside
                     that rectangle to have grown while a rectangle 200 px away from it did not move,
                     press Ctrl+S, and require the field's value in the saved copy to be that token
                     with its three neighbours still empty. -Control types nothing and keeps every
                     verdict, so it exits 1.
      formtab        the same sample, two fields and one Tab: click alpha, press Tab, type -- the
                     token has to come back out of BETA; then Shift+Tab and a second token, which has
                     to come back out of ALPHA. Which field holds which token is read out of the saved
                     file, so "Tab moved the focus" is a reading about two values rather than an
                     inference from one. -Control skips the Tab and still expects beta to hold it.
      formcheck      the checkbox: click it, require the ink inside its 16x16 rectangle to go from 0
                     to a filled square (the sample's /Yes appearance really paints, because the
                     headless generator's two states are both empty streams and toggling those moves
                     no pixel at all), require the star, save, and require /V to read Yes; then click
                     it again and require the same four things in reverse. -Control clicks nothing and
                     still expects Yes.
      formime        the composition-string route, measured rather than assumed: ImmGetContext on our
                     own window, ImmSetCompositionString with two Chinese characters as GCS_COMPSTR,
                     ImmNotifyIME(NI_COMPOSITIONSTR, CPS_COMPLETE), and then the field's value read
                     back out of the saved file. Every step's own answer is printed -- including the
                     last error, the layouts that exist, the app thread's active layout and a control
                     window in this shell -- because "the commit string never arrived" has to be
                     distinguishable from "there was no input context" and from "the call was refused".
                     Beside it, as labelled comparisons and never as substitutes, a DIFFERENT pair of
                     characters sent one KEYEVENTF_UNICODE pair at a time (a per-character WM_CHAR,
                     which is NOT the route G1 sends a composition string down) and pinyin letters as
                     real virtual keys plus a space. The three routes write into three different
                     fields so the saved file says which route's text reached which field. The verdict
                     is on the composition string: if it does not land, this mode exits 1 with all
                     three readings printed. -Control requests no context on the app window and sends
                     no composition, still expects the two characters in gamma, and exits 1.
      formcombo      the combo box, three rounds: (1) press its own dropdown button, require the ink
                     BELOW the field to go from 0 to a painted list whose rows are dark ink bands,
                     press the second band and require the saved file's value to be the file's second
                     option; (2) the same list opened by Alt+Down while the field holds the keyboard,
                     third band pressed, the file's THIRD option required -- a different answer on
                     purpose, so a stale round-1 value cannot pass it; (3) the editable combo: click,
                     type a made-up token, save, read it back. Every round ends in Ctrl+S and in the
                     file's own answer, so "the popup painted" and "the value persisted" stay two
                     separate readings. -Control presses no dropdown button and no key: it focuses the
                     field, saves, requires the value to stay empty, and exits 1.
      formlist       the multi-select list box: a plain press on the first row, then Ctrl+press on the
                     third, then Ctrl+S -- and the saved file must answer BOTH rows, read off the
                     per-option lines pdfprobe prints (a multi-select /V is an array and the ordinary
                     value read answers only its first entry). Row positions are ink bands inside the
                     field's own rectangle, measured off the picture, not assumed from PDFium's row
                     height. -Control's second press is plain, so the file answers the last row only
                     and the mode exits 1 -- which is what makes the Ctrl in the positive a cause.
      signedsave     the incremental save of a signed document, on a screen: fill the sample's text
                     field, press Ctrl+S, and require four things about the BYTES of the copy -- the
                     value read back is what was typed, the file is longer, its first N bytes are the
                     file that was there before the save (N = that earlier length), and the two spans
                     the signature's /ByteRange names hash the same on both sides of it. -Control
                     keeps every verdict and rotates a page first, which is the one edit that cannot
                     be appended: the whole file is rewritten, so the prefix assertion has to exit 1.
                     The rotate box's own before-the-press sentence is read on the way (it is the
                     other half of sec.122's signed-document work and had never been on screen).
      signedlock     what a certification signature permits, judged where the reader meets it: the
                     status line at open, a structure box's accept button, and a click on a field.
                     -ExpectDocMdp says which sample to build and what it must show -- 1 refuses both
                     the field and the box, 2 fills the field and refuses the box with the sentence
                     naming form filling, 0 (the unsigned-of-certification half) lets both through and
                     says nothing about certification. The button is read in the ROTATE box with the
                     current page named, not in the delete box: an untouched delete box greys its
                     accept until a range is typed, and on a one-page sample every range that names a
                     page names the only page and so greys it again for a reason that has nothing to
                     do with signatures -- measured on the P=0 round, where the button was grey on a
                     document that refuses nothing. A verdict that cannot tell those two documents
                     apart is decoration (sec.99), so the delete box keeps the sentences, which need
                     no range. The focus question is a DELTA of the ink in the field's own rectangle
                     between the same place without the keyboard and with it:
                     the sample's field carries a value from the first byte, so formfill's absolute
                     caret count would answer "the caret is here" for a field nobody touched.
                     -Control builds the 0 sample and keeps -ExpectDocMdp 2's verdicts, so it exits 1
                     on the accept button being grey -- the reading that says a grey button is a
                     cause and not a stuck control.
      signaturepanel handoff-08 J1: the sidebar's signatures page, read off the window. -Sample
                     names which of the ten samples the maker writes (xyzpdf_signaturecheck_smoke.exe
                     beside -Exe, into a fresh directory under %TEMP%\xyzpdf-gui-probe); the round
                     opens it, opens the sidebar through its own View-menu row, switches to the tab,
                     and judges each top-level row's TEXT against the verdict that sample must
                     produce on a machine whose root store does not hold the sample's own CA -- so
                     valid, expired and appended-one say the certificate is not trusted or has
                     expired, tampered and narrowed say the content changed, x509-rsa-sha1 and
                     broken-blob say it cannot be checked. Every row is expanded and its detail
                     lines read: appended-one must say there is one incremental update after the
                     signature, appended-two two, and a file that was never appended to must say
                     nothing about revisions. The first row is then clicked and the tree's selection
                     must move to it (the page itself cannot move -- the sample is one page); if it
                     does not, the row had keyboard focus and is enabled-not-selectable, which is
                     reported as a product defect and exits 1 after the picture is taken, so the
                     other readings are not lost. A PrintWindow picture of the tab strip and the panel
                     is kept, its crop anchored to the window's FRAME (sec.89), and the crop is
                     asserted to hold every sidebar tab and the tree. -Sample none is the negative
                     half: no sample is made, -Doc must be a file with no signature, and the
                     signatures tab must be absent -- with the other three tabs asserted first, so a
                     sidebar that never opened cannot pass as "the tab is gone".
                     -Control changes one route per sample and keeps the verdict: tampered expects
                     the certificate sentence (the handoff's control), valid expects an incremental
                     line on a file with none, narrowed expects two rows, appended-one expects two
                     updates, x509-rsa-sha1 skips the tab switch, broken-blob anchors the crop to
                     the client rectangle instead of the frame, and none expects a tab that is not
                     there. Each of those must exit 1.

    THE FORM MODES. formfill/formtab/formcheck/formime are handoff-05 H2: form filling had been
    measured only in headless tests (sec.118 says so), so nothing in it had ever been on a screen.
    formcombo/formlist are handoff-06 H5: H1's choice fields (two combos, one multi-select list)
    existed only in headless tests when H2 wrote the first four. All six aim at a sample this
    harness makes itself -- %TEMP%\xyzpdf-gui-probe\form-screen.pdf, written by
    xyzpdf_pdfprobe --write-form-sample beside the -Exe that was handed in: four text fields, a
    checkbox, a list box and two combos on one unrotated page, each with a label in the page's own
    content stream. Field rectangles are read back out of the copy the app opened (--fields), never
    typed into this file, so the arithmetic that turns one into a screen point is checked against
    the same bytes the reader is looking at. All six press Ctrl+S, so all six are save modes and
    all six open the guard's copy rather than whatever -Doc named.

    THE SAVE GUARD. The three modes above press Ctrl+S, Ctrl+Shift+S or the Save answer of a
    question box, and every one of those writes a PDF. None of them is allowed to write a file
    somebody handed this script: each copies -Doc into %TEMP%\xyzpdf-gui-probe\save-<random>\ under
    a name made up here, opens THAT, and hashes the original before the app starts and again at the
    end of the run -- and on every Bail in between, because the mode that gives up halfway is the
    mode that might already have written. A tests\fixtures path is read and copied; it is not
    opened. -Installed is refused for these three unless -OpenViaPicker names the copy.

    THE SIGNED MODES. signedsave/signedlock are handoff-07 I2: sec.122 put a signed document's
    incremental save, its before-the-press sentence and its DocMDP doors into headless tests, and
    sec.123 read the status line and one delete-box sentence on the packaged copy -- nothing else had
    ever been on a screen. Both aim at a sample xyzpdf_pdfprobe --write-signed-sample makes beside
    -Exe: one text field with the value 'before' in it and one /Sig field whose /V names a signature
    dictionary whose /ByteRange was computed off the bytes as installed (tests/formsample.h's
    writeSigned, the generator sec.122 pinned). The flag says whether it also certifies the document
    and with what /P, so the difference between a run that must hold its bytes and one that must not
    is one number on the command line rather than a second generator. Like the form modes they open
    the guard's copy, never what -Doc named.

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
      ... -Mode saveinplace -Doc xyzpdf/tests/fixtures/text.pdf
      ... -Mode saveinplace -Control -Doc xyzpdf/tests/fixtures/text.pdf   (also exit 1)
      ... -Mode closeprompt -Doc xyzpdf/tests/fixtures/text.pdf
      ... -Mode closeprompt -Control -Doc xyzpdf/tests/fixtures/text.pdf   (also exit 1)
      ... -Mode saveas -Doc xyzpdf/tests/fixtures/text.pdf
      ... -Mode saveas -Control -Doc xyzpdf/tests/fixtures/text.pdf   (also exit 1)
      ... -Mode formfill -Doc xyzpdf/tests/fixtures/text.pdf -Log H:/tmp/formfill-en.txt
      ... -Mode formfill -Control -Doc xyzpdf/tests/fixtures/text.pdf   (also exit 1)
      ... -Mode formtab -Doc xyzpdf/tests/fixtures/text.pdf
      ... -Mode formcheck -Doc xyzpdf/tests/fixtures/text.pdf
      ... -Mode formime -Doc xyzpdf/tests/fixtures/text.pdf
      ... -Mode formcombo -Doc xyzpdf/tests/fixtures/text.pdf -Log H:/tmp/formcombo-en.txt
      ... -Mode formcombo -Control -Doc xyzpdf/tests/fixtures/text.pdf   (also exit 1)
      ... -Mode formlist -Doc xyzpdf/tests/fixtures/text.pdf -Log H:/tmp/formlist-en.txt
      ... -Mode formlist -Control -Doc xyzpdf/tests/fixtures/text.pdf   (also exit 1)
      ... -Mode signedsave -Doc xyzpdf/tests/fixtures/text.pdf -Log H:/tmp/signedsave-en.txt
      ... -Mode signedsave -Control -Doc xyzpdf/tests/fixtures/text.pdf   (also exit 1)
      ... -Mode signedlock -ExpectDocMdp 1 -Doc xyzpdf/tests/fixtures/text.pdf
      ... -Mode signedlock -ExpectDocMdp 2 -Doc xyzpdf/tests/fixtures/text.pdf
      ... -Mode signedlock -ExpectDocMdp 0 -Doc xyzpdf/tests/fixtures/text.pdf   (the negative half)
      ... -Mode signedlock -ExpectDocMdp 2 -Control -Doc xyzpdf/tests/fixtures/text.pdf   (exit 1)
      ... -Mode wordexport -Doc xyzpdf/tests/fixtures/text.pdf -Target 'XYZPDF one'
      ... -Mode wordexport -Control -Doc xyzpdf/tests/fixtures/text.pdf   (exit 1: nothing pressed)
      wordexport writes its .docx into %TEMP%\xyzpdf-gui-probe\wordexport-<random>\ and checks it with
      tools/check_docx.py; -Target names words the export must carry. Installed: -Installed -OpenViaPicker.
      ... -Mode excelexport -Doc xyzpdf/tests/fixtures/table.pdf
      ... -Mode pptexport -Doc xyzpdf/tests/fixtures/mixed.pdf
      ... -Mode searchablepdf -Doc xyzpdf/tests/fixtures/scan.pdf
      The three handoff-09 conversions take the same route as wordexport (one function, Export-Round):
      excelexport presses Export in the options box first and checks with check_xlsx.py, pptexport
      checks with check_pptx.py, and searchablepdf reads the new PDF back with pdfprobe --text (it
      must carry invisible characters) and hashes -Doc before and after. Each takes -Control (exit 1).
      ... -Mode signaturepanel -Sample valid -Language zh_CN -ExpectLanguage zh -Log H:/tmp/j1-valid-zh.txt
      ... -Mode signaturepanel -Sample none -Doc xyzpdf/tests/fixtures/text.pdf -Language zh_CN
      ... -Mode signaturepanel -Sample tampered -Control   (the handoff's control; exit 1)
      The signature-panel mode makes its own ten samples in %TEMP%\xyzpdf-gui-probe\sign-<random>\
      and replaces -Doc with the one -Sample names; -Sample none makes nothing and reads the file
      -Doc names. A packaged run is pointed at the sample through its own picker automatically,
      because the directory name is drawn in this run and no caller could type it in advance.
      The six form modes replace -Doc with the sample they make, so the path handed in is only what
      the guard hashes and copies; a fixtures path is fine and is not opened. The two signed modes do
      the same, with --write-signed-sample and the /P that -ExpectDocMdp asks for.
      The three above write nothing outside %TEMP%\xyzpdf-gui-probe\save-<random>\: -Doc is copied
      and never opened, so a fixtures path is a perfectly good thing to hand them.

    -Log takes a UTF-8 file, which is the only way to read a Chinese interface back from this
    console (it is GBK, so the window title alone arrives as mojibake). Write such a path with
    forward slashes: a backslashed one handed in from Git Bash loses its separators and the log
    lands as ./tmpfoo.txt in the repository root.
#>
param(
  [ValidateSet('inventory', 'menudump', 'exportstatus', 'mergedonor', 'splitsagain', 'saverename', 'saveinplace', 'closeprompt', 'saveas', 'langswitch', 'formpage', 'uacache', 'openpicker', 'outlinetree', 'tabstrip', 'bookdialog', 'deletesummary', 'pathforms', 'propsdialog', 'findwhole', 'snapshot', 'strippreview', 'mnemonics', 'translatesetup', 'xfanotice', 'replynotice', 'formfill', 'formtab', 'formcheck', 'formime', 'formcombo', 'formlist', 'signedsave', 'signedlock', 'wordexport', 'signaturepanel', 'excelexport', 'pptexport', 'searchablepdf')]
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
  # And so do the three save modes: saveinplace leaves Ctrl+S unpressed, closeprompt closes a
  # document nothing has edited, saveas presses neither the chord nor the menu row -- each keeps the
  # verdict it is written against, so each has to exit 1 rather than quietly report a save it never
  # made. The six form modes read it the same way: formfill types nothing, formtab skips the Tab,
  # formcheck clicks nothing, formime sends no composition, formcombo never opens the dropdown,
  # The six form modes read it the same way: formfill types nothing, formtab skips the Tab,
  # formcheck clicks nothing, formime sends no composition, formcombo never opens the dropdown,
  # formlist's second press drops the Ctrl -- and every one of them keeps its verdict.
  # The two signed modes keep every verdict too and change the document under them: signedsave
  # rotates a page before the save, so the bytes that must not move do, and signedlock builds a
  # document whose signature certifies nothing while still expecting the doors it refuses to be shut.
  # signaturepanel reads it per sample: one route is deliberately wrong while every verdict it was
  # written against is kept (the table in the help block), because each of its assertions -- the
  # verdict text, the row count, the revision sentence and its number, the selected tab, the row's
  # own selection, the crop's geometry, and the absent tab on an unsigned file -- has to be shown
  # able to fail rather than merely able to print a line.
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
  # signedlock: the /P of the certification signature the sample is built with, and therefore what
  # the screen must show. 1 = nothing may change (the field refuses the click, the box refuses the
  # press), 2 = form filling only (the field takes it, the box still refuses and says why in the
  # sentence naming form filling), 0 = signed but not certified, which is the negative half: both
  # doors open and no certification sentence appears anywhere. The sample is MADE with this number
  # in it, so the value is a request to the generator as much as an expectation -- a mode that took a
  # document as given could not be run against all three. -Control overrides it to 0 and keeps the
  # expectation it is given, which is what makes the grey accept a verdict rather than a reading.
  # signedsave always builds the 0 sample: its verdict is about which bytes moved, not about which
  # edits are allowed.
  [string]$ExpectDocMdp = '',
  # signaturepanel: which of the ten samples the maker writes and the panel is judged against.
  # 'none' is the negative half -- no sample is made, and the file -Doc names must show no
  # Signatures tab at all. The names are the ones tests/signaturecheck_smoke.cpp writes.
  [ValidateSet('valid', 'tampered', 'narrowed', 'expired', 'appended-one', 'appended-two',
               'x509-rsa-sha1', 'broken-blob', 'gap-at-front', 'token-unreadable', 'none')]
  [string]$Sample = 'valid',
  # The interface a round must be speaking. Every needle list in this file is bilingual, so a
  # mode that assumed the language it asked for would pass in the other one and report a screen
  # it never read (sec.92's lesson). 'any' accepts either and is what every older mode means.
  [ValidateSet('any', 'en', 'zh')]
  [string]$ExpectLanguage = 'any',
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
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public static IntPtr Hit(int x, int y) { POINT p = new POINT(); p.x = x; p.y = y; return WindowFromPoint(p); }
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

  // ---- the input the form modes send ----
  //
  // SendInput rather than SendKeys for the characters: SendKeys spells half the
  // alphabet as a shortcut (+, ^, %, ~, (, ), [, ], {, } all mean something else to
  // it), so a made-up token to type into a field would have to be escaped by a rule
  // the harness would then be trusting. One KEYEVENTF_UNICODE pair per character has
  // no such table, and it is what Windows itself hands a window for a character with
  // no virtual key -- which arrives as WM_CHAR, the route a text field's FORM_OnChar
  // reads. That is also why formime prints this route BESIDE the IMM32 one instead of
  // instead of it: a committed string and a per-character WM_CHAR are two different
  // measurements (handoff-05 H2 says so, and G1 is the reason -- the composition
  // string goes through FORM_ReplaceSelection, one character at a time would break it
  // into pieces).
  //
  // INPUT is 40 bytes on x64 (a DWORD type, then a union aligned to 8 whose largest
  // member, MOUSEINPUT, is 32). SendInput compares cbSize against its own sizeof and
  // answers 0 with ERROR_INVALID_PARAMETER when they differ, so the size is spelled
  // out rather than left to Marshal.SizeOf of a keyboard-only struct.
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT {
    public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo;
  }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT {
    public int dx; public int dy; public int mouseData; public uint dwFlags; public uint time;
    public IntPtr dwExtraInfo;
  }
  [StructLayout(LayoutKind.Explicit, Size = 40)] public struct INPUT {
    [FieldOffset(0)] public uint type;
    [FieldOffset(8)] public KEYBDINPUT ki;
    [FieldOffset(8)] public MOUSEINPUT mi;
  }
  [DllImport("user32.dll", SetLastError = true)]
  public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
  [DllImport("user32.dll")] public static extern uint MapVirtualKeyW(uint uCode, uint uMapType);
  public const uint INPUT_KEYBOARD = 1;
  public const uint KEYEVENTF_KEYUP = 0x0002;
  public const uint KEYEVENTF_UNICODE = 0x0004;
  public const ushort VK_SHIFT = 0x10;
  public static uint LastError() { return (uint)Marshal.GetLastWin32Error(); }

  // One character, down and up. Returns what SendInput says it INSERTED, so a
  // caller can tell "sent" from "accepted" -- the difference this suite keeps
  // having to learn (sec.88: a posted BM_CLICK is not an accepted dialog).
  public static uint TypeChar(char c) {
    INPUT[] inp = new INPUT[2];
    inp[0].type = INPUT_KEYBOARD; inp[0].ki.wScan = c; inp[0].ki.dwFlags = KEYEVENTF_UNICODE;
    inp[1].type = INPUT_KEYBOARD; inp[1].ki.wScan = c;
    inp[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    return SendInput(2, inp, 40);
  }
  // One virtual key, optionally held between a Shift down and up: Tab, Shift+Tab, and the Esc that
  // lets go of a form field. wScan carries the scan code MapVirtualKeyW gives that key, because the
  // character route says nothing about the virtual-key route: measured with wScan left 0 and
  // KEYEVENTF_SCANCODE unset, two Backspaces typed into a focused form field deleted nothing while
  // KEYEVENTF_UNICODE characters inserted fine. With the scan code filled in, Esc does arrive
  // (formtab's key control: caret 24 px -> 0). KEYEVENTF_SCANCODE stays unset -- Qt reads the
  // virtual key.
  public static uint PressVk(ushort vk, bool withShift) {
    int n = withShift ? 4 : 2;
    INPUT[] inp = new INPUT[n];
    int i = 0;
    if (withShift) {
      inp[i].type = INPUT_KEYBOARD; inp[i].ki.wVk = VK_SHIFT;
      inp[i].ki.wScan = (ushort)MapVirtualKeyW(VK_SHIFT, 0); i++;
    }
    inp[i].type = INPUT_KEYBOARD; inp[i].ki.wVk = vk;
    inp[i].ki.wScan = (ushort)MapVirtualKeyW(vk, 0); i++;
    inp[i].type = INPUT_KEYBOARD; inp[i].ki.wVk = vk; inp[i].ki.dwFlags = KEYEVENTF_KEYUP;
    inp[i].ki.wScan = (ushort)MapVirtualKeyW(vk, 0); i++;
    if (withShift) {
      inp[i].type = INPUT_KEYBOARD; inp[i].ki.wVk = VK_SHIFT;
      inp[i].ki.dwFlags = KEYEVENTF_KEYUP;
      inp[i].ki.wScan = (ushort)MapVirtualKeyW(VK_SHIFT, 0); i++;
    }
    return SendInput((uint)n, inp, 40);
  }
  public const uint INPUT_MOUSE = 0;
  public const uint MOUSEEVENTF_WHEEL = 0x0800;
  public const ushort VK_CONTROL = 0x11;
  public const ushort VK_ALT = 0x12;
  // One modifier held around OTHER input, for the two gestures H5 needs and PressVk cannot
  // express: Ctrl+click on a multi-select list's second row, and Alt+Down on a combo box.
  // Sent as its own SendInput call rather than bundled with the mouse/key event, because the
  // click is W32::Click (mouse_event) and merging the two APIs in one array is not something
  // this file has measured; the key state is read when Qt processes the later message, which
  // is exactly what a held key is.
  public static uint KeyDown(ushort vk) {
    INPUT[] inp = new INPUT[1];
    inp[0].type = INPUT_KEYBOARD; inp[0].ki.wVk = vk;
    inp[0].ki.wScan = (ushort)MapVirtualKeyW(vk, 0);
    return SendInput(1, inp, 40);
  }
  public static uint KeyUp(ushort vk) {
    INPUT[] inp = new INPUT[1];
    inp[0].type = INPUT_KEYBOARD; inp[0].ki.wVk = vk; inp[0].ki.dwFlags = KEYEVENTF_KEYUP;
    inp[0].ki.wScan = (ushort)MapVirtualKeyW(vk, 0);
    return SendInput(1, inp, 40);
  }
  // Alt held between the two halves of one virtual key -- the shape the product reads for
  // "open the combo's list" (pageview passes the event and the backend turns Alt+Down into a
  // press on the field's own dropdown button, measured in handoff-05 H1).
  public static uint PressAltVk(ushort vk) {
    INPUT[] inp = new INPUT[4];
    inp[0].type = INPUT_KEYBOARD; inp[0].ki.wVk = VK_ALT;
    inp[0].ki.wScan = (ushort)MapVirtualKeyW(VK_ALT, 0);
    inp[1].type = INPUT_KEYBOARD; inp[1].ki.wVk = vk;
    inp[1].ki.wScan = (ushort)MapVirtualKeyW(vk, 0);
    inp[2].type = INPUT_KEYBOARD; inp[2].ki.wVk = vk; inp[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inp[2].ki.wScan = (ushort)MapVirtualKeyW(vk, 0);
    inp[3].type = INPUT_KEYBOARD; inp[3].ki.wVk = VK_ALT; inp[3].ki.dwFlags = KEYEVENTF_KEYUP;
    inp[3].ki.wScan = (ushort)MapVirtualKeyW(VK_ALT, 0);
    return SendInput(4, inp, 40);
  }
  // Ctrl+wheel, because that is the ONLY zoom route this product has (pageview.cpp's wheelEvent
  // takes Ctrl+wheel and nothing else; there is no zoom menu and no Ctrl+/- shortcut). The form
  // modes need the whole page in one picture, and a page opened at fit-width is twice as tall as
  // the viewport. mouseData is the wheel delta, negative for out.
  public static uint Wheel(int x, int y, int delta, bool ctrl) {
    SetCursorPos(x, y);
    int n = ctrl ? 3 : 1;
    INPUT[] inp = new INPUT[n];
    int i = 0;
    if (ctrl) { inp[i].type = INPUT_KEYBOARD; inp[i].ki.wVk = VK_CONTROL; i++; }
    inp[i].type = INPUT_MOUSE; inp[i].mi.mouseData = delta; inp[i].mi.dwFlags = MOUSEEVENTF_WHEEL; i++;
    if (ctrl) {
      inp[i].type = INPUT_KEYBOARD; inp[i].ki.wVk = VK_CONTROL; inp[i].ki.dwFlags = KEYEVENTF_KEYUP; i++;
    }
    return SendInput((uint)n, inp, 40);
  }

  // ---- IMM32, for the one mode that has to go through an input method ----
  // Every call is declared with SetLastError where the platform sets one, because
  // the reading H2 asks for is each step's own answer: a composition string that
  // never arrives has to be distinguishable from one that was refused, and from a
  // window that has no input context at all.
  [DllImport("imm32.dll", SetLastError = true)] public static extern IntPtr ImmGetContext(IntPtr h);
  [DllImport("imm32.dll")] public static extern bool ImmReleaseContext(IntPtr h, IntPtr himc);
  [DllImport("imm32.dll", SetLastError = true)] public static extern bool ImmGetOpenStatus(IntPtr himc);
  [DllImport("imm32.dll", SetLastError = true)] public static extern bool ImmSetOpenStatus(IntPtr himc, bool fOpen);
  [DllImport("imm32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern bool ImmSetCompositionString(IntPtr himc, uint dwIndex, string lpComp, uint dwCompLen,
                                                    IntPtr lpRead, uint dwReadLen);
  [DllImport("imm32.dll", SetLastError = true)]
  public static extern bool ImmNotifyIME(IntPtr himc, uint dwCommand, uint dwParam, IntPtr dwExtraInfo);
  [DllImport("imm32.dll", CharSet = CharSet.Unicode, SetLastError = true, EntryPoint = "ImmGetCompositionStringW")]
  public static extern int ImmGetCompositionString(IntPtr himc, uint dwIndex, StringBuilder lpBuf, uint dwBufLen);
  [DllImport("imm32.dll", SetLastError = true)] public static extern IntPtr ImmGetDefaultIMEWnd(IntPtr h);
  [DllImport("imm32.dll", SetLastError = true)]
  public static extern bool ImmAssociateContextEx(IntPtr h, IntPtr himc, uint dwFlags);
  public const uint GCS_COMPSTR = 0x0008;
  public const uint GCS_RESULTSTR = 0x0800;
  public const uint NI_COMPOSITIONSTR = 0x3;
  public const uint CPS_COMPLETE = 0x1;
  public const uint IACE_DEFAULT = 0x0010;

  // ---- which input locale the window's thread is in, and how to ask about it ----
  // Printed beside the context readings, because "ImmGetContext answered zero" is about a window and
  // not about the machine: the layouts that exist and the one the app's own thread is using are what
  // turn a zero into a diagnosis. ImmIsIME is left out on purpose -- it answered True for the plain US
  // layout here, so it does not mean what its name says on this machine and nothing below uses it.
  [DllImport("user32.dll")] public static extern IntPtr GetFocus();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("user32.dll")] public static extern IntPtr GetKeyboardLayout(uint idThread);
  [DllImport("user32.dll")] public static extern int GetKeyboardLayoutList(int nBuff, IntPtr[] lpList);
  [DllImport("imm32.dll", CharSet = CharSet.Unicode, SetLastError = true, EntryPoint = "ImmGetDescriptionW")]
  public static extern uint ImmGetDescription(IntPtr hkl, StringBuilder lpszDescription, uint uBufLen);
  public const uint WM_INPUTLANGCHANGEREQUEST = 0x0050;
  public static IntPtr[] KeyboardLayouts() {
    int n = GetKeyboardLayoutList(0, null);
    if (n <= 0) return new IntPtr[0];
    IntPtr[] a = new IntPtr[n];
    int got = GetKeyboardLayoutList(n, a);
    if (got < 0) got = 0;
    if (got < n) Array.Resize(ref a, got);
    return a;
  }
  public static string LayoutDesc(IntPtr hkl) {
    var b = new StringBuilder(256);
    uint n = ImmGetDescription(hkl, b, 256);
    return n > 0 ? b.ToString() : "";
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
  Kill-ProbeApp
  if ($script:guardBroken) { Say '  GUARD: the file handed to the probe MOVED during this run (hashes above)' }
  exit 1
}
function Kill-ProbeApp() {
  # -ErrorAction SilentlyContinue covers Get-Process; it does not cover the METHOD. Kill() throws
  # when the process is already on its way out, which is precisely what the Discard and Save answers
  # of the three-question box do to a last tab -- so a run that did everything right still printed
  # an exception block (measured 2026-10-23, closeprompt's English round). Swallowing it is safe
  # because a process that survives this is caught two lines later: the next launch polls for a
  # window naming its own document and Bails when none appears.
  foreach ($p in @(Get-Process -Name xyzpdf -ErrorAction SilentlyContinue)) {
    try { $p.Kill() } catch { }
  }
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
  Kill-ProbeApp
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
  Kill-ProbeApp
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

  # A full path is always typed into the file-name box, even when its row is in the list: the row
  # click aims at the row's on-screen rectangle, and the last file of a folder (text.pdf in
  # tests/fixtures) can sit below the visible part of the list -- the click then lands beside it and
  # Open opens nothing (sec.130: four rounds in a row, after save boxes had changed the remembered
  # size of the dialog). Typing the path was proven in sec.90 and does not depend on the view.
  if ($rowName -match '[\\:]') {
    if (-not $target) { Say ('  no row naming [' + $base + '] in the folder the picker opened at') }
    Say '  a full path was given: typing it into the file-name box'
    if (-not (TypePathIntoPicker $pk $rowName)) { return $false }
  } elseif (-not $target) {
    Say ('  no row naming [' + $base + '] in the folder the picker opened at')
    Say '  -OpenViaPicker gave a bare name, so the row click is the only route'
    return $false
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
# The four form modes (handoff-05 H2) are on this list too: each of them changes a field and then
# presses Ctrl+S, so each of them writes a PDF, and the file they write has to be one this script
# made. They do not even open the -Doc that was handed in -- see the sample hook below.
$script:saveModes = @('saveinplace', 'closeprompt', 'saveas', 'saverename',
                      'formfill', 'formtab', 'formcheck', 'formime', 'formcombo', 'formlist',
                      'signedsave', 'signedlock')
$script:formModes = @('formfill', 'formtab', 'formcheck', 'formime', 'formcombo', 'formlist')
# The two signed modes carry their own document the same way the form modes do, and for the same
# reason: nothing on this machine is a signed PDF, and a mode that has to say which bytes moved has
# to know what the file looked like before the save.
$script:signedModes = @('signedsave', 'signedlock')
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

# The pdfprobe that belongs to the -Exe this run drives, or nothing. Both sample-making families
# need it -- the six form modes and the two signed ones -- and a second copy of the check is how one
# of them ends up building beside a different exe than it screenshots (sec.87's rule, read against
# this harness: parallel branches hand in their own -Exe).
function Probe-Tool([string]$tag) {
  $tool = Join-Path (Split-Path -Parent $Exe) 'xyzpdf_pdfprobe.exe'
  if (-not (Test-Path -LiteralPath $tool)) {
    Bail ('  no pdfprobe beside [' + $Exe + '] -- -Mode ' + $tag + ' needs it to make and to read ' +
          'the sample; build the tree first (python tools/build_all_tests.py)')
  }
  return $tool
}

# The four form modes carry their own document, because no PDF with form fields exists on this
# machine and a mode that aims at a field has to know where the field is. The sample is MADE, by
# the pdfprobe tool that sits beside -Exe, into this harness's scratch -- never into tests/fixtures
# (a directory whose name contains 'fixtures' is refused by the sample writers), and never over a
# file that is already there: --write-form-sample refuses to overwrite, because a tool whose job is
# making samples must not be able to write a document somebody handed it. -Doc is replaced, and the
# guard below then copies the sample like any other -Doc, so what the app opens and what the modes
# read back is still a copy with a name made up in this run.
if ($script:formModes -contains $Mode) {
  $script:probeTool = Probe-Tool $Mode
  if (-not (Test-Path $scratch)) { New-Item -ItemType Directory -Path $scratch | Out-Null }
  $sample = Join-Path $scratch 'form-screen.pdf'
  if (Test-Path -LiteralPath $sample) { Remove-Item -LiteralPath $sample -Force }
  $made = & $script:probeTool --write-form-sample $sample 2>&1
  foreach ($l in @($made)) { Say ('  sample: ' + $l) }
  if ($LASTEXITCODE -ne 0) {
    Bail ('  --write-form-sample exited ' + $LASTEXITCODE + ', so there is no sample for -Mode ' + $Mode)
  }
  if (-not (Test-Path -LiteralPath $sample)) {
    Bail ('  --write-form-sample reported success but [' + $sample + '] is not on disk')
  }
  if ($Doc) { Say ('  -Doc [' + $Doc + '] is replaced by the sample this mode makes') }
  $Doc = $sample
}

# ---- the signed modes' sample -------------------------------------------------------------
# The same discipline one step on: no signed PDF exists on this machine either, and a mode whose
# verdict is 'these bytes did not move' has to know what the file looked like before the save, so
# the file is made here rather than hunted for. --write-signed-sample writes the shape sec.122 built
# in tests/formsample.h -- one text field carrying 'before' and one /Sig field whose /V names a
# signature dictionary whose /ByteRange was computed off the bytes as installed -- and it refuses a
# path that already exists, so the previous round's file is deleted here first: that Remove-Item is
# pointed at one path this script just built, under %TEMP%\xyzpdf-gui-probe, and at nothing else.
#
# Which /P the generator is asked for is the mode's expectation, with one deliberate exception:
# -Control builds the UNCERTIFIED document (P=0, signed but no /Perms /DocMDP) while keeping the
# verdicts it was given. That is what makes the grey accept a claim about the certification rather
# than about a button this harness cannot read -- the same screen, the same assertions, a document
# that has no reason to refuse, and the round has to exit 1.
#
# signedsave asks for P=0 on purpose and ignores -ExpectDocMdp: its verdict is about which bytes
# moved, and a certified document would refuse the rotation its control performs before the save --
# the control would then be testing the door, not the bytes.
if ($script:signedModes -contains $Mode) {
  $script:probeTool = Probe-Tool $Mode
  if (-not (Test-Path $scratch)) { New-Item -ItemType Directory -Path $scratch | Out-Null }
  $script:expectP = $ExpectDocMdp
  if ($Mode -eq 'signedlock') {
    if ($Control -and -not $script:expectP) {
      $script:expectP = '2'
      Say '  signedlock -Control with no -ExpectDocMdp: the P=2 verdicts are the ones kept'
    }
    if ($script:expectP -notmatch '^[0-3]$') {
      Bail ('  -Mode signedlock needs -ExpectDocMdp 0, 1, 2 or 3 (the /P of the certification the ' +
            'sample is built with); got [' + $ExpectDocMdp + ']')
    }
  } else {
    $script:expectP = '0'
    if ($ExpectDocMdp) {
      Say ('  signedsave judges bytes, not doors: -ExpectDocMdp [' + $ExpectDocMdp +
           '] is ignored and the sample is uncertified')
    }
  }
  $script:builtP = $script:expectP
  if ($Control) { $script:builtP = '0' }
  $sample = Join-Path $scratch ('signed-screen-p' + $script:builtP + '.pdf')
  if (Test-Path -LiteralPath $sample) { Remove-Item -LiteralPath $sample -Force }
  $made = & $script:probeTool --write-signed-sample $sample --docmdp $script:builtP 2>&1
  foreach ($l in @($made)) { Say ('  sample: ' + $l) }
  if ($LASTEXITCODE -ne 0) {
    Bail ('  --write-signed-sample exited ' + $LASTEXITCODE + ', so there is no sample for -Mode ' + $Mode)
  }
  if (-not (Test-Path -LiteralPath $sample)) {
    Bail ('  --write-signed-sample reported success but [' + $sample + '] is not on disk')
  }
  # The number the file carries, as opposed to the one that was asked for. pdfprobe already compares
  # the two and exits 1 when they disagree, but the modes' own verdicts are written against
  # $script:builtP, so a run has to be able to say which document it is holding.
  Say ('  signed sample: built with /P ' + $script:builtP + ', the round judges it as /P ' +
       $script:expectP + $(if ($Control) { '  (CONTROL)' } else { '' }))
  if ($Doc) { Say ('  -Doc [' + $Doc + '] is replaced by the signed sample this mode makes') }
  $Doc = $sample
}

# ---- the signature panel's samples ----------------------------------------------------------
# handoff-08 J1. The panel is read from a document that carries signatures, and nothing on this
# machine does -- except the ten samples tests/signaturecheck_smoke.cpp writes (one per verdict and
# one per shape). It writes them into the directory XYZPDF_SIGNATURE_SAMPLE_DIR names, and refuses
# a directory that already exists: a reading of last round's file is exactly the vacuous receipt
# this suite keeps catching. So every round gets a directory of its own, made empty here.
#
# The binary sits in build-local-release\tests while the DLLs it needs were deployed one level up
# by windeployqt; Windows searches the exe's own directory and PATH, not its parent. The directory
# of the tree being driven goes on PATH for this child only -- a different tree is what parallel
# worktrees must not share (sec.87's rule, read against this harness).
if ($Mode -eq 'signaturepanel') {
  if ($Sample -eq 'none') {
    Say '  negative round: -Sample none makes nothing; the file -Doc names must carry no signature'
    if (-not $Installed -and -not (Test-Path -LiteralPath $Doc)) {
      Bail ('  no document at ' + $Doc + ' -- -Sample none reads the file -Doc names')
    }
  } else {
    $tool = Join-Path (Join-Path (Split-Path -Parent $Exe) 'tests') 'xyzpdf_signaturecheck_smoke.exe'
    if (-not (Test-Path -LiteralPath $tool)) {
      Bail ('  no [' + $tool + '] beside -Exe -- -Mode signaturepanel makes its samples with it; ' +
            'build the tree first (python tools/build_all_tests.py)')
    }
    $dir = Join-Path $scratch ('sign-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    if (Test-Path -LiteralPath $dir) {
      Bail ('  the sample directory [' + $dir + '] already exists; the maker refuses to write into one')
    }
    $deployDir = Split-Path -Parent $Exe
    $oldPath = $env:PATH
    $made = @()
    $code = 0
    $env:XYZPDF_SIGNATURE_SAMPLE_DIR = $dir
    try {
      $env:PATH = $deployDir + ';' + $oldPath
      Say ('  making the ten signature samples in [' + $dir + '] with [' + $tool + ']')
      $made = @(& $tool 2>&1)
      $code = $LASTEXITCODE
    } finally {
      $env:PATH = $oldPath
      Remove-Item Env:\XYZPDF_SIGNATURE_SAMPLE_DIR -ErrorAction SilentlyContinue
    }
    $bad = @($made | Where-Object { $_ -like '*FAIL*' -or $_ -like '*SETUP*' })
    foreach ($l in $bad) { Say ('  sample: ' + $l) }
    foreach ($l in @($made | Select-Object -Last 2)) { Say ('  sample tail: ' + $l) }
    if ($code -ne 0) {
      Bail ('  the sample maker exited ' + $code + ', so there is no sample for -Mode signaturepanel')
    }
    $path = Join-Path $dir ($Sample + '.pdf')
    if (-not (Test-Path -LiteralPath $path)) {
      Bail ('  the maker reported success but [' + $path + '] is not on disk')
    }
    Say ('  sample [' + $path + '] ' + (Get-Item -LiteralPath $path).Length + ' bytes')
    if ($Doc) { Say ('  -Doc [' + $Doc + '] is replaced by the sample this mode makes') }
    $Doc = $path
  }
  if ($Installed) {
    if ($Sample -eq 'none') {
      if (-not $OpenViaPicker) { $OpenViaPicker = $Doc }
      if ([System.IO.Path]::GetFileName($OpenViaPicker) -ne
          [System.IO.Path]::GetFileName($Doc)) {
        Bail ('  -OpenViaPicker names [' + [System.IO.Path]::GetFileName($OpenViaPicker) +
              '] but -Doc names [' + [System.IO.Path]::GetFileName($Doc) + ']')
      }
    } else {
      if ($OpenViaPicker) {
        Say ('  -OpenViaPicker [' + $OpenViaPicker + '] is replaced: the sample''s directory is ' +
             'drawn in this run, so no caller could have named it')
      }
      $OpenViaPicker = $Doc
    }
    Say ('  packaged run: the picker is given [' + $OpenViaPicker + ']')
  }
}

# 'saverename' stays an accepted -Mode name because rounds since sec.44 are quoted by it in the
# comments below and in the notes the main session keeps; the mode itself is Save As now. PowerShell
# has no multi-label switch clause ('a','b' { } does not parse and @('a','b') never matches), so the
# alias is folded here -- before the guard reads $Mode, and before the switch does.
if ($Mode -eq 'saverename') { $Mode = 'saveas' }

if ($script:saveModes -contains $Mode) {
  # Armed before the launch, because the launch itself is what must not name the original.
  $Doc = Guard-Arm $Doc
  if ($Installed) {
    # A packaged instance opens its own document through the picker, so the only way it can reach a
    # file is the row or path the caller names. Under a save mode that has to be the copy made above.
    # The copy's name is drawn fresh on every run, so no caller can name it in advance: when
    # -OpenViaPicker is not given, the picker is handed the copy itself (sec.118 -- the first version
    # refused instead, which made every packaged save run Bail before it started).
    if (-not $OpenViaPicker) {
      $script:OpenViaPicker = $script:guardCopy
      $OpenViaPicker = $script:guardCopy
      Say ('  packaged save run: the picker gets the copy [' + $script:guardCopy + ']')
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
function Rotate-Box([int]$procId) {
  # Document / Rotate Pages opened, the 'Current page' radio picked, and the box left standing: the
  # caller either presses the accept (Rotate-CurrentPage below) or reads the button and walks away
  # (signedlock, whose verdict IS the button). One walk for both, because a second copy of an
  # open-the-box route is how a mode ends up working in only one language (sec.87).
  #
  # The radio is picked before the button is handed back because the button's state is only a claim
  # about the document once a page has actually been named: an untouched box greys its accept for a
  # reason that has nothing to do with signatures (rotatepagesdialog.cpp wants `present > 0`), which
  # is the same vacuity sec.72 had to step around in the bookmark box.
  TopMenu $procId @('Document', (CjStr 0x6587, 0x6863)) | Out-Null
  $row = RowAny $procId @('Rotate Pages', (CjStr 0x65CB, 0x8F6C, 0x9875, 0x9762))
  if (-not $row) { Bail '  no Rotate Pages row under Document in either language' }
  Say ('  menu row |' + $row.Current.Name + '| enabled=' + $row.Current.IsEnabled)
  ClickAt $row
  $dlg = WaitClass $procId 'RotatePagesDialog' 12
  if (-not $dlg) { Bail '  the rotate dialog did not open' }
  # The box's own summary, kept for the caller: a structure box on a signed file says before the
  # press what the save will cost, and that sentence is one of the readings the signed modes judge.
  $summary = LongestText $dlg
  Say ('  rotate box (' + @(FindType $dlg $CT::Text).Count + ' Text element(s)): |' +
       $summary.Replace("`r", ' ').Replace("`n", ' / ') + '|')
  $cur = MatchOne $dlg $CT::RadioButton @('Current page', (CjStr 0x5F53, 0x524D, 0x9875))
  if (-not $cur) { Bail '  no Current page radio in either language' }
  Say ('  picking |' + $cur.Current.Name + '|')
  ClickAt $cur
  Start-Sleep -Milliseconds 600
  $go = MatchOne $dlg $CT::Button @('Rotate these pages', (CjStr 0x65CB, 0x8F6C, 0x8FD9, 0x4E9B, 0x9875))
  if (-not $go) {
    foreach ($b in @(FindType $dlg $CT::Button)) { Say ('    button |' + $b.Current.Name + '|') }
    Bail '  no rotate accept among the buttons in either language'
  }
  foreach ($b in @(FindType $dlg $CT::Button)) {
    Say ('    button |' + $b.Current.Name + '| enabled=' + $b.Current.IsEnabled)
  }
  return @{ dlg = $dlg; accept = $go; lit = $go.Current.IsEnabled; summary = $summary }
}
function Rotate-CurrentPage([int]$procId) {
  # ... and pressed: the route strippreview has used since sec.109, wanted by the save modes because
  # an edit that shows up in the saved BYTES is what makes 'the save happened' a disk reading and not
  # one line of status text.
  $box = Rotate-Box $procId
  if (-not $box.lit) { Bail '  the rotate accept is not lit after picking the current page' }
  Say ('  recording: |' + $box.accept.Current.Name + '|')
  ClickAt $box.accept
  if (-not (WaitClassGone $procId 'RotatePagesDialog' 20)) { Bail '  the rotate dialog stayed open after the press' }
  Start-Sleep -Milliseconds 600
  $script:rotateSummary = $box.summary
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

# ---- the form modes' shared routes ------------------------------------------------------
# Six modes, one page, eight fields (the four text boxes and the checkbox of handoff-05 H2, plus
# the list box and the two combos handoff-06 H5 added), and every one of them does the same three
# things: aim at a field on the screen, change it, and read the file back. The routes live here
# once because sec.87's lesson is about harnesses as much as products -- a second copy of a route
# is how one of the six ends up working in only one language.
function UForm([string]$s) {
  # The PowerShell twin of pdfprobe's asciiSafe(): printable ASCII stays as it is, everything else
  # becomes U+XXXX, space separated. A field value has to be compared exactly and this file must
  # stay pure ASCII, so both sides of a comparison are escaped and the log line survives a GBK
  # console. The two implementations are one rule written twice; the IME mode's verdict is the
  # first thing that goes wrong if they ever disagree.
  $out = ''
  foreach ($ch in $s.ToCharArray()) {
    $cp = [int]$ch
    if ($cp -ge 0x20 -and $cp -lt 0x7F) { $out += $ch; continue }
    if ($out.Length -gt 0 -and -not $out.EndsWith(' ')) { $out += ' ' }
    $out += ('U+{0:X4} ' -f $cp)
  }
  return $out.Trim()
}
function Read-FormFields([string]$path, [string]$tag, [int]$minFields = 8) {
  # The file's own answer, straight from the library the reader uses. Every mode reads the copy
  # twice -- once before it touches anything (the target must be empty, or the run is measuring a
  # stale file) and once after the save (the value must be what was typed).
  $raw = & $script:probeTool --fields $path 2>&1
  foreach ($l in @($raw)) { Say ('  ' + $tag + ' |' + $l + '|') }
  if ($LASTEXITCODE -ne 0) { Bail ('  ' + $tag + ': --fields exited ' + $LASTEXITCODE + ' on [' + $path + ']') }
  $fields = @()
  # Per-option reads, one list per choice field. A multi-select list's /V is an array and the
  # FIELD line above carries only its first entry (handoff-05 H1 measured that), so "both rows
  # are still selected" cannot be a value comparison -- it is these lines or nothing.
  $script:ffOptions = @{}
  foreach ($l in @($raw)) {
    $t = "$l"
    if ($t -match '^pages:\s*(\d+)\s*$') { $script:ffPages = [int]$Matches[1] }
    elseif ($t -match '^formtype:\s*(\d+)\s*$') { $script:ffFormType = [int]$Matches[1] }
    elseif ($t -match '^formenv:\s*(\S+)\s*$') { $script:ffFormEnv = $Matches[1] }
    elseif ($t -match '^PAGE\s+(\d+)\s+mediabox=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)\s+rotate=(-?\d+)\s+width=([-\d.]+)\s+height=([-\d.]+)\s+annots=(\d+)\s*$') {
      $script:ffMediaL = [double]$Matches[2]; $script:ffMediaB = [double]$Matches[3]
      $script:ffMediaR = [double]$Matches[4]; $script:ffMediaT = [double]$Matches[5]
      $script:ffRotate = [int]$Matches[6]
    }
    elseif ($t -match '^OPTION\s+field=\[(.*?)\]\s+index=(\d+)\s+label=\[(.*?)\]\s+selected=(\d+)\s*$') {
      $of = $Matches[1]
      if (-not $script:ffOptions.ContainsKey($of)) {
        $script:ffOptions[$of] = New-Object System.Collections.Generic.List[object]
      }
      $script:ffOptions[$of].Add([pscustomobject]@{
        index = [int]$Matches[2]; label = $Matches[3]; selected = [int]$Matches[4] })
    }
    elseif ($t -match '^FIELD\s+(\d+)\s+page=(\d+)\s+name=\[(.*?)\]\s+type=(\S+)\s+flags=(-?\d+)\s+rect\(l,b,r,t\)=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)\s+value=\[(.*)\]\s*$') {
      $fields += [pscustomobject]@{
        idx = [int]$Matches[1]; page = [int]$Matches[2]; name = $Matches[3]; type = $Matches[4];
        flags = [int]$Matches[5]; l = [double]$Matches[6]; b = [double]$Matches[7];
        r = [double]$Matches[8]; t = [double]$Matches[9]; value = $Matches[10]
      }
    }
  }
  if ($script:ffPages -ne 1) { Bail ('  ' + $tag + ': the copy has ' + $script:ffPages + ' pages; the screen arithmetic below is for one') }
  if ($script:ffRotate -ne 0) { Bail ('  ' + $tag + ': the page is rotated ' + $script:ffRotate + '; the mapping is for rotate 0') }
  if ($script:ffFormEnv -ne 'opened') { Bail ('  ' + $tag + ': the form environment answered [' + $script:ffFormEnv + '], so no field can be filled') }
  if ($script:ffFormType -ne 1) { Bail ('  ' + $tag + ': formtype is ' + $script:ffFormType + ', not the AcroForm 1 the sample carries') }
  if (@($fields).Count -lt $minFields) {
    Bail ('  ' + $tag + ': only ' + @($fields).Count + ' fields came back; this mode wants at least ' +
          $minFields + ' and the copy answers [' + ((@($fields) | ForEach-Object { $_.name }) -join ', ') + ']')
  }
  $script:ffFields = $fields
  return $fields
}
function Field-At([string]$name, [string]$tag) {
  foreach ($f in @($script:ffFields)) { if ($f.name -eq $name) { return $f } }
  Bail ('  ' + $tag + ': no field named [' + $name + ']; the copy carries ' +
        ((@($script:ffFields) | ForEach-Object { $_.name }) -join ', '))
}
function Selected-Labels([string]$name, [string]$tag) {
  # Which options the file says are selected, in the file's own option order. The absence of
  # OPTION lines is a Bail rather than an empty answer: a choice field the library could not
  # enumerate and a choice field with nothing picked would otherwise both read as "none", and
  # the name of the difference is a defect in the reading (sec.82's same-row companion rule).
  if (-not $script:ffOptions.ContainsKey($name)) {
    Bail ('  ' + $tag + ': no OPTION line for [' + $name + '] -- the file carries no option list this ' +
          'reader could read, so "nothing is selected" would be a statement about the reading')
  }
  # The options are walked without @(). On this PowerShell (5.1.26100.9711) wrapping a
  # System.Collections.Generic.List[object] in an array subexpression throws
  # "'parameter type mismatch'" (an ArgumentException with no mention of the list), while the same
  # wrapper over List[string] or ArrayList is fine and a plain foreach over List[object] is fine
  # too -- measured standalone before the fix, after this mode's first round answered "0 options"
  # while the log above carried all four. The reading was the harness, not the file.
  $list = $script:ffOptions[$name]
  $out = @()
  foreach ($o in $list) { if ($o.selected -eq 1) { $out += $o.label } }
  return $out
}
function Option-Labels([string]$name, [string]$tag) {
  # The same list, unselected or not: a click on "the second row" is a click on whatever the
  # file calls the second option, and that has to be asked rather than assumed here.
  if (-not $script:ffOptions.ContainsKey($name)) { Bail ('  ' + $tag + ': no OPTION line for [' + $name + ']') }
  $list = $script:ffOptions[$name]
  $out = @()
  foreach ($o in $list) { $out += $o.label }
  return $out
}
function Page-Shot([string]$tag) {
  # One picture of the page area, plus the rectangle the paper occupies inside it. Everything a
  # form mode clicks or counts is computed from those two, so both are printed and both are
  # guarded. The framing is the OUTER window rect and the crop origin is the frame's top-left
  # (sec.89: the picture is anchored to the frame, so a crop computed from the client rectangle
  # lands on the border and the tab strip), and formpage's two geometric guards are kept -- a
  # colour rule for "that is the window frame" refused a legitimately dark sidebar (sec.91).
  $win = Window $app.Id
  if (-not $win) { Bail ('  ' + $tag + ': no window to shoot') }
  if (-not (ForegroundIs $win)) {
    # Only when it is not ours already. TakeForeground opens a menu and Escapes it, and a field
    # holding the keyboard would lose it to that Esc -- so a shot taken after typing must find the
    # window already in front, which it does, because the click that aimed the field put it there.
    if (-not (TakeForeground $app.Id)) { Bail ('  ' + $tag + ': could not take the foreground') }
    $win = Window $app.Id
    # The title bar, and by its FRAME rectangle: the UIA rectangle of a Qt top level is the client
    # area (sec.74), so "client row 12" is the toolbar, and a click there is a tool button -- which
    # would change what the very next press on the page means.
    $h0 = [IntPtr]$win.Current.NativeWindowHandle
    $g0 = [W32]::Rect($h0)
    [W32]::Click([int](($g0.left + $g0.right) / 2), [int]($g0.top + 12))
    Start-Sleep -Milliseconds 700
    $win = Window $app.Id
  }
  $hwnd = [IntPtr]$win.Current.NativeWindowHandle
  $gr = [W32]::Rect($hwnd)
  $co = [W32]::ClientOrigin($hwnd)
  $wr = $win.Current.BoundingRectangle
  if ([math]::Abs([int]$wr.X - $co.x) -gt 2 -or [math]::Abs([int]$wr.Y - $co.y) -gt 2) {
    Bail ('  ' + $tag + ': the top-level UIA rectangle disagrees with the Win32 client origin by ' +
          ([int]$wr.X - $co.x) + ',' + ([int]$wr.Y - $co.y) + ' -- the crop and the picture would not be in the same units')
  }
  $best = $null; $bestTop = -1; $maxArea = 0
  $cands = New-Object System.Collections.Generic.List[object]
  foreach ($type in @($CT::Document, $CT::Pane, $CT::Image)) {
    foreach ($e in @(FindType $win $type)) {
      $r = $e.Current.BoundingRectangle
      $area = [int]($r.Width * $r.Height)
      if ($area -gt 40000) {
        Say ('  ' + $tag + ' candidate ' + $type.ProgrammaticName + ' ' + [int]$r.X + ',' + [int]$r.Y +
             ' ' + [int]$r.Width + 'x' + [int]$r.Height)
        $cands.Add($e) | Out-Null
        if ($area -gt $maxArea) { $maxArea = $area }
      }
    }
  }
  foreach ($e in $cands) {
    $r = $e.Current.BoundingRectangle
    if (($r.Width * $r.Height) -lt (0.6 * $maxArea)) { continue }
    if ([int]$r.Y -gt $bestTop) { $bestTop = [int]$r.Y; $best = $e }
  }
  if (-not $best) { Bail ('  ' + $tag + ': no element big enough to be the page') }
  $r = $best.Current.BoundingRectangle
  $fw = [int]($gr.right - $gr.left)
  $fh = [int]($gr.bottom - $gr.top)
  $winBmp = New-Object System.Drawing.Bitmap($fw, $fh)
  $wg = [System.Drawing.Graphics]::FromImage($winBmp)
  $hdc = $wg.GetHdc()
  $painted = [W32]::PrintWindow($hwnd, $hdc, 2)
  $wg.ReleaseHdc($hdc)
  $left = [int][math]::Floor($r.X - $gr.left)
  $top = [int][math]::Floor($r.Y - $gr.top)
  $width = [int][math]::Ceiling($r.Width)
  $height = [int][math]::Ceiling($r.Height)
  if ($left -lt 0) { $width += $left; $left = 0 }
  if ($top -lt 0) { $height += $top; $top = 0 }
  if ($left + $width -gt $fw) { $width = $fw - $left }
  if ($top + $height -gt $fh) { $height = $fh - $top }
  if ($width -lt 100 -or $height -lt 100) {
    $winBmp.Dispose()
    Bail ('  ' + $tag + ': the page crop is ' + $width + 'x' + $height + ' inside a ' + $fw + 'x' + $fh +
          ' window picture -- the geometry disagrees, so no number from it means anything')
  }
  $bmp = $winBmp.Clone((New-Object System.Drawing.Rectangle($left, $top, $width, $height)), $winBmp.PixelFormat)
  $winBmp.Dispose()
  $wg.Dispose()
  if ($left -lt ($co.x - $gr.left) -or $top -lt ($co.y - $gr.top)) {
    $bmp.Dispose()
    Bail ('  ' + $tag + ': the crop starts at bitmap ' + $left + ',' + $top + ' but the client area only ' +
          'begins at ' + ($co.x - $gr.left) + ',' + ($co.y - $gr.top) + ' -- the picture is anchored to the ' +
          'window frame, so these pixels are not the page')
  }
  $tabBottom = -1
  foreach ($t in @(FindType $win $CT::TabItem)) {
    $tr = $t.Current.BoundingRectangle
    if ([int]$tr.Bottom -gt $tabBottom) { $tabBottom = [int]$tr.Bottom }
  }
  if ($tabBottom -ge 0 -and $top -lt ($tabBottom - $gr.top)) {
    $bmp.Dispose()
    Bail ('  ' + $tag + ': the crop starts at window row ' + $top + ', above the bottom of the tab strip (' +
          ($tabBottom - $gr.top) + ') -- a difference there is file names, not the page')
  }
  $rectAll = New-Object System.Drawing.Rectangle(0, 0, $width, $height)
  $lock = $bmp.LockBits($rectAll, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $bytes = New-Object 'byte[]' ($lock.Stride * $height)
  [System.Runtime.InteropServices.Marshal]::Copy($lock.Scan0, $bytes, 0, $bytes.Length)
  $stride = $lock.Stride
  $bmp.UnlockBits($lock)
  $shot = Join-Path $scratch ('formshot-' + $tag + '.png')
  $bmp.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  # Where the paper is, read off the picture rather than assumed. The view's background is
  # (214,218,222) and the paper (255,255,255), so near-white -- every channel >= 245 -- separates
  # them. The OUTERMOST near-white pixel in a row is the wrong reading and was measured wrong once
  # already: the scrollbar's track throws a white sliver near the right edge, so the paper is the
  # LONGEST contiguous run of near-white columns, and the same for rows.
  $colPaper = New-Object 'bool[]' $width
  $colRatio = New-Object 'double[]' $width
  $maxCol = 0.0
  for ($x = 0; $x -lt $width; $x++) {
    $w = 0; $n = 0
    for ($y = 0; $y -lt $height; $y += 6) {
      $o = $y * $stride + $x * 4; $n++
      if ($bytes[$o] -ge 245 -and $bytes[$o + 1] -ge 245 -and $bytes[$o + 2] -ge 245) { $w++ }
    }
    $colRatio[$x] = $w / $n
    if ($colRatio[$x] -gt $maxCol) { $maxCol = $colRatio[$x] }
  }
  $rowPaper = New-Object 'bool[]' $height
  $rowRatio = New-Object 'double[]' $height
  $maxRow = 0.0
  for ($y = 0; $y -lt $height; $y++) {
    $w = 0; $n = 0
    for ($x = 0; $x -lt $width; $x += 6) {
      $o = $y * $stride + $x * 4; $n++
      if ($bytes[$o] -ge 245 -and $bytes[$o + 1] -ge 245 -and $bytes[$o + 2] -ge 245) { $w++ }
    }
    $rowRatio[$y] = $w / $n
    if ($rowRatio[$y] -gt $maxRow) { $maxRow = $rowRatio[$y] }
  }
  # Paper is whatever is within 60% of the BEST column (and row), not whatever clears a fixed
  # bar: a page zoomed out to fit is narrower than the crop, so a fixed 80% bar would be dragged
  # under by the grey background on both sides and report zero rows of paper. The longest run is
  # still what separates the page from the scrollbar's white sliver.
  for ($x = 0; $x -lt $width; $x++) { $colPaper[$x] = ($maxCol -gt 0 -and $colRatio[$x] -ge (0.6 * $maxCol)) }
  for ($y = 0; $y -lt $height; $y++) { $rowPaper[$y] = ($maxRow -gt 0 -and $rowRatio[$y] -ge (0.6 * $maxRow)) }
  $bestS = 0; $bestL = 0; $runS = 0; $runL = 0
  for ($x = 0; $x -lt $width; $x++) {
    if ($colPaper[$x]) { if ($runL -eq 0) { $runS = $x }; $runL++ }
    else { if ($runL -gt $bestL) { $bestL = $runL; $bestS = $runS }; $runL = 0 }
  }
  if ($runL -gt $bestL) { $bestL = $runL; $bestS = $runS }
  $pageX = $bestS; $pageW = $bestL
  $bestS = 0; $bestL = 0; $runS = 0; $runL = 0
  for ($y = 0; $y -lt $height; $y++) {
    if ($rowPaper[$y]) { if ($runL -eq 0) { $runS = $y }; $runL++ }
    else { if ($runL -gt $bestL) { $bestL = $runL; $bestS = $runS }; $runL = 0 }
  }
  if ($runL -gt $bestL) { $bestL = $runL; $bestS = $runS }
  $pageY = $bestS; $pageH = $bestL
  Say ('  ' + $tag + ': PrintWindow ' + $painted + ', crop ' + $width + 'x' + $height + ' at window ' +
       $left + ',' + $top + '; paper ' + $pageW + 'x' + $pageH + ' at crop ' + $pageX + ',' + $pageY +
       ' (best column ' + [math]::Round($maxCol, 2) + ', best row ' + [math]::Round($maxRow, 2) +
       '); kept at ' + $shot)
  if ($pageW -lt 150 -or $pageH -lt 150) {
    Bail ('  ' + $tag + ': the longest near-white run is ' + $pageW + 'x' + $pageH + ' -- that is not a page ' +
          'in a ' + $width + 'x' + $height + ' crop, so nothing can be aimed at')
  }
  if ($script:formPageRect) {
    $p = $script:formPageRect
    if ([math]::Abs($p.px - $pageX) -gt 3 -or [math]::Abs($p.py - $pageY) -gt 3 -or
        [math]::Abs($p.pw - $pageW) -gt 3 -or [math]::Abs($p.ph - $pageH) -gt 3) {
      Bail ('  ' + $tag + ': the paper moved between shots (' + $p.px + ',' + $p.py + ' ' + $p.pw + 'x' +
            $p.ph + ' -> ' + $pageX + ',' + $pageY + ' ' + $pageW + 'x' + $pageH + ') -- a click aimed with ' +
            'the first picture lands somewhere else in the second')
    }
  } else {
    $script:formPageRect = @{ px = $pageX; py = $pageY; pw = $pageW; ph = $pageH }
  }
  return @{ tag = $tag; width = $width; height = $height; bytes = $bytes; stride = $stride;
            pageX = $pageX; pageY = $pageY; pageW = $pageW; pageH = $pageH;
            frameLeft = $gr.left; frameTop = $gr.top; cropLeft = $left; cropTop = $top; shot = $shot }
}
function Map-Rect($shot, [double]$ux0, [double]$uy0, [double]$ux1, [double]$uy1) {
  # User space -- origin at the mediabox's bottom-left, the space FS_RECTF and the field rectangles
  # both speak -- to bitmap columns and rows. zoom is the paper's own width over the mediabox's, so
  # a wrong zoom shows up as a value landing in the wrong field, not as a silent number.
  #
  # The bitmap columns and rows are CROP-relative, and the crop does not start at the window's
  # top-left (measured here: 8,151 -- a frame and a tab strip above it). So a screen point is the
  # frame origin PLUS the crop origin plus the bitmap position. Dropping the middle term was
  # measured once and it is the quietest failure in this file: the click lands in the toolbar, the
  # field never takes the keyboard, the typing goes to nothing, and every count in the picture --
  # which is in crop units and therefore correct -- still reads as if the aim were true.
  $zoom = $shot.pageW / ($script:ffMediaR - $script:ffMediaL)
  $bx0 = $shot.pageX + ($ux0 - $script:ffMediaL) * $zoom
  $bx1 = $shot.pageX + ($ux1 - $script:ffMediaL) * $zoom
  $by0 = $shot.pageY + ($script:ffMediaT - $uy1) * $zoom
  $by1 = $shot.pageY + ($script:ffMediaT - $uy0) * $zoom
  return @{ zoom = $zoom; bx0 = $bx0; bx1 = $bx1; by0 = $by0; by1 = $by1;
            sx = $shot.frameLeft + $shot.cropLeft + ($bx0 + $bx1) / 2;
            sy = $shot.frameTop + $shot.cropTop + ($by0 + $by1) / 2 }
}
function Assert-InCrop($shot, $m, [string]$what) {
  if ($m.by1 -lt 0 -or $m.by0 -gt ($shot.height - 1) -or $m.bx1 -lt 0 -or $m.bx0 -gt ($shot.width - 1)) {
    Bail ('  ' + $what + ' maps to bitmap rows ' + [int]$m.by0 + '-' + [int]$m.by1 + ' of a ' +
          $shot.height + '-row crop -- it is not on screen, so a click there hits paper')
  }
  # The same claim in the units a click is actually sent in: the point must fall inside the crop's
  # own rectangle on screen, not merely inside the picture. This is the half that would have caught
  # the missing crop origin above -- the bitmap test passes either way, because the picture and the
  # bitmap agree with each other and both are wrong about the window.
  $l = $shot.frameLeft + $shot.cropLeft
  $t = $shot.frameTop + $shot.cropTop
  if ([int]$m.sx -lt $l -or [int]$m.sx -ge ($l + $shot.width) -or
      [int]$m.sy -lt $t -or [int]$m.sy -ge ($t + $shot.height)) {
    Bail ('  ' + $what + ' maps to screen ' + [int]$m.sx + ',' + [int]$m.sy + ' but the page crop is ' +
          $shot.width + 'x' + $shot.height + ' at screen ' + $l + ',' + $t + ' -- the click would land ' +
          'outside the page, so the aim is wrong and nothing after it is evidence')
  }
}
function Region-Ink($shot, $m) {
  # Non-white sample points inside a mapped rectangle. Step 1: at the zoom these modes settle at,
  # the checkbox is a dozen pixels across and a step of 2 would count half of it.
  $n = 0
  $x0 = [int][math]::Max(0, [math]::Floor($m.bx0))
  $x1 = [int][math]::Min($shot.width - 1, [math]::Ceiling($m.bx1))
  $y0 = [int][math]::Max(0, [math]::Floor($m.by0))
  $y1 = [int][math]::Min($shot.height - 1, [math]::Ceiling($m.by1))
  for ($y = $y0; $y -le $y1; $y++) {
    for ($x = $x0; $x -le $x1; $x++) {
      $o = $y * $shot.stride + $x * 4
      if ($shot.bytes[$o] -lt 245 -or $shot.bytes[$o + 1] -lt 245 -or $shot.bytes[$o + 2] -lt 245) { $n++ }
    }
  }
  return $n
}
function Caret-Map([string]$tag) {
  # Which field holds the keyboard, read off the picture -- ALL of them, because "the caret is not in
  # beta" and "the caret is nowhere" are different findings and only the second one means the keyboard
  # was dropped. This sample's text fields paint no border and no background of their own (no /AP, no
  # /MK, no /BS -- sec.118 measured that a widget with nothing to draw contributes nothing to FFLDraw),
  # so the ONE thing that separates a focused field from its neighbour on screen is the caret PDFium
  # draws inside it: 24 px of ink in the field's own rectangle, and 0 in the same rectangle before the
  # click. The labels are drawn at x >= 270 and every field ends at x = 260, so nothing else lands in
  # these rectangles. Two shots a blink apart and the larger of the two, because a caret is allowed to
  # blink and one shot could catch it dark.
  #
  # LIMIT, measured the hard way: this counts INK, so a field holding typed text looks like a field
  # holding a caret for as long as the text is there (both shots see it). It therefore only answers
  # while the fields it is asked about are empty; after the first token is typed, which field has what
  # is read out of the saved file (distinct tokens), not from here.
  #
  # SECOND LIMIT, measured when H5 added the choice fields: only a TEXT field paints nothing around
  # an empty value. On the same picture, with nothing typed anywhere, the multi-select list box read
  # 420 px (its four option labels) and the two combo boxes read 274 and 242 px (their own chrome and
  # arrow) -- so a caret map over every field would answer "the keyboard is in the list box" for
  # every text field this suite ever asks about. The map is over the text fields on purpose; the
  # choice modes read their values out of the saved file instead of out of pixels.
  $textFields = @($script:ffFields | Where-Object { $_.type -eq 'TEXTFIELD' })
  $s1 = Page-Shot ($tag + '-1')
  $first = @{}
  foreach ($f in $textFields) {
    $first[$f.name] = Region-Ink $s1 (Map-Rect $s1 $f.l $f.b $f.r $f.t)
  }
  Start-Sleep -Milliseconds 700
  $s2 = Page-Shot ($tag + '-2')
  $m = [ordered]@{}
  foreach ($f in $textFields) {
    $second = Region-Ink $s2 (Map-Rect $s2 $f.l $f.b $f.r $f.t)
    $m[$f.name] = [math]::Max($first[$f.name], $second)
  }
  $parts = @()
  foreach ($k in $m.Keys) { $parts += ($k + '=' + $m[$k]) }
  Say ('  ' + $tag + ': ink in every field rectangle -- ' + ($parts -join ' '))
  return $m
}
function Caret-Holders($m) {
  # 10 px is not a judgement call: a caret is 24 and an empty field's rectangle is 0.
  $h = @()
  foreach ($k in $m.Keys) { if ($m[$k] -gt 10) { $h += $k } }
  return $h
}
function Caret-Expect($m, [string[]]$want, [string]$what) {
  $got = @(Caret-Holders $m)
  $same = ($got.Count -eq $want.Count)
  if ($same) {
    foreach ($w in $want) { if (-not ($got -contains $w)) { $same = $false } }
  }
  if (-not $same) {
    $was = if ($got.Count -eq 0) { 'no field at all' } else { $got -join ', ' }
    $should = if ($want.Count -eq 0) { 'no field' } else { $want -join ', ' }
    Bail ('  ' + $what + ': the caret is in ' + $was + ', and it should be in ' + $should)
  }
}
function Focus-Now {
  # Who UIA says has the keyboard, as a second opinion on the caret. The caret says which FORM field
  # PDFium thinks is focused; this says which widget Windows thinks is focused, and Tab's two
  # candidate mechanisms part company exactly there -- if Qt's focus chain took the key the page is
  # no longer the focus widget (and its focusOutEvent releases the field), while if PDFium took the
  # key the page keeps the keyboard and only the field is gone. A reading, not a verdict: Qt's
  # QAccessible may answer with the window for everything, in which case it says so and nothing here
  # depends on it.
  try {
    $e = [System.Windows.Automation.AutomationElement]::FocusedElement
    if (-not $e) { return '(none)' }
    return ($e.Current.ControlType.ProgrammaticName + ' [' + $e.Current.Name + ']')
  } catch {
    return '(unreadable: ' + $_.Exception.Message + ')'
  }
}
function Field-Ink([string]$tag, $f) {
  $s = Page-Shot $tag
  return Region-Ink $s (Map-Rect $s $f.l $f.b $f.r $f.t)
}
function Layout-Label($hkl) {
  # One keyboard layout as value + description. The value is the whole reading -- HKLs are opaque and
  # the description is empty for every layout on this machine -- so both are printed and neither is
  # interpreted.
  if ($null -eq $hkl -or $hkl -eq [IntPtr]::Zero) { return '0x00000000 (none)' }
  $v = [uint32]($hkl.ToInt64() -band [int64]0xFFFFFFFF)
  return ('0x{0:X8} desc=[{1}]' -f $v, [W32]::LayoutDesc($hkl))
}
function Zoom-WholePage([string]$tag) {
  # A document opens at fit-width, and at fit-width an A4 page is ~1700 px tall in a ~720 px
  # viewport: the bottom half of the page, and with it the checkbox, is simply not on screen. The
  # product's only zoom route is Ctrl+wheel (pageview.cpp's wheelEvent takes Ctrl+wheel and nothing
  # else -- there is no zoom menu and no Ctrl+/- shortcut), so that is what this uses, one notch at
  # a time, and it stops when the PICTURE says the whole page is in: the paper's height over its
  # width is then the mediabox's own ratio, which a page cut off at the crop's bottom edge can
  # never show. A notch is 1/1.25 of the factor (ZoomController::Step), so five of them carry
  # fit-width down to a third.
  $script:formPageRect = $null
  $want = ($script:ffMediaT - $script:ffMediaB) / ($script:ffMediaR - $script:ffMediaL)
  for ($k = 1; $k -le 10; $k++) {
    # The paper is SUPPOSED to move between these shots, so the "the paper moved" guard is disarmed
    # for each one and armed again by the shot that ends the loop.
    $script:formPageRect = $null
    $shot = Page-Shot ($tag + '-zoom' + $k)
    $ratio = $shot.pageH / $shot.pageW
    Say ('  ' + $tag + ' zoom pass ' + $k + ': paper ' + $shot.pageW + 'x' + $shot.pageH + ', ratio ' +
         [math]::Round($ratio, 3) + ' against the page''s ' + [math]::Round($want, 3))
    if ([math]::Abs($ratio - $want) -lt 0.03 -and $shot.pageY -gt 2 -and
        ($shot.pageY + $shot.pageH) -lt ($shot.height - 2)) {
      return $shot
    }
    [W32]::Wheel([int]($shot.frameLeft + $shot.cropLeft + $shot.width / 2),
                 [int]($shot.frameTop + $shot.cropTop + $shot.height / 2), -120, $true) | Out-Null
    Start-Sleep -Milliseconds 700
  }
  Bail ('  ' + $tag + ': ten notches of Ctrl+wheel never put the whole page inside the crop')
}
function Click-Mapped($shot, $m, [string]$tag, [string]$what, [switch]$Ctrl) {
  # One press at a mapped place, with the two readings that make the click's arrival
  # evidence: the point is inside the page crop on screen, and the window at that point is
  # this process. Both were learned the expensive way (sec.89's crop origin; sec.84's click
  # landing on somebody else's window), so they are shared rather than retyped -- H5's
  # dropdown rows and list rows are aimed at the same kind of place a field is.
  Assert-InCrop $shot $m ($tag + ' ' + $what)
  $hit = [W32]::Hit([int]$m.sx, [int]$m.sy)
  $hitPid = [uint32]0
  [W32]::GetWindowThreadProcessId($hit, [ref]$hitPid) | Out-Null
  Say ('  ' + $tag + ': at screen ' + [int]$m.sx + ',' + [int]$m.sy + ' the window is class=[' +
       [W32]::Cls($hit) + '] pid=' + $hitPid + ' text=[' + [W32]::Text($hit) + ']')
  if ($hitPid -ne [uint32]$app.Id) {
    Bail ('  ' + $tag + ': the point maps to pid ' + $hitPid + ' (' + [W32]::Cls($hit) +
          '), not to this app ' + $app.Id + ' -- the click would land on somebody else''s window')
  }
  Say ('  ' + $tag + ': clicking ' + $what + ' at screen ' + [int]$m.sx + ',' + [int]$m.sy +
       ' (bitmap ' + [int]$m.bx0 + '-' + [int]$m.bx1 + ' x ' + [int]$m.by0 + '-' + [int]$m.by1 + ')' +
       $(if ($Ctrl) { ' with Ctrl held' } else { '' }))
  if ($Ctrl) {
    # The modifier goes down before the press and up after the release, and each call's own
    # count is checked: a Ctrl that SendInput did not insert would make this an ordinary
    # click, and the control run would then be measuring the wrong gesture (sec.99's rule).
    $down = [W32]::KeyDown([W32]::VK_CONTROL)
    if ($down -ne 1) { Bail ('  ' + $tag + ': SendInput inserted ' + $down + ' of 1 Ctrl-down events, last error ' + ([W32]::LastError())) }
    Start-Sleep -Milliseconds 200
  }
  [W32]::Click([int]$m.sx, [int]$m.sy)
  if ($Ctrl) {
    Start-Sleep -Milliseconds 200
    $up = [W32]::KeyUp([W32]::VK_CONTROL)
    if ($up -ne 1) { Bail ('  ' + $tag + ': SendInput inserted ' + $up + ' of 1 Ctrl-up events, last error ' + ([W32]::LastError())) }
  }
  Start-Sleep -Milliseconds 600
}
function Click-Field($shot, $f, [string]$tag) {
  Click-Mapped $shot (Map-Rect $shot $f.l $f.b $f.r $f.t) $tag ('field ' + $f.name)
}
function Click-Band($shot, $m, $band, [string]$tag, [string]$what, [switch]$Ctrl) {
  # The centre of one ink band as a click: bands are found by Ink-Bands in bitmap rows, so
  # this converts nothing -- it adds the frame and crop origin the same way Map-Rect does,
  # and the shared Click-Mapped then does the geometric and process checks.
  $bx = ($m.bx0 + $m.bx1) / 2
  $by = [double]$band.cy
  $point = @{ bx0 = $bx; bx1 = $bx; by0 = $by; by1 = $by;
              sx = $shot.frameLeft + $shot.cropLeft + $bx;
              sy = $shot.frameTop + $shot.cropTop + $by }
  Click-Mapped $shot $point $tag $what -Ctrl:$Ctrl
}
function Ink-Bands($shot, $m, [string]$tag) {
  # Rows inside a mapped rectangle that carry DARK ink, grouped into bands. This is how the
  # modes find a combo's popup rows and a list box's own rows without assuming PDFium's row
  # height: the layout is measured off the picture. The threshold is darkness, not
  # non-whiteness, on purpose -- the popup's own background is light, so a non-white count
  # would make the whole box one band; the glyphs are what separates them (and a selected row,
  # measured on the open combo, is a solid dark bar rather than a light highlight).
  #
  # The first version counted a row as ink from two dark pixels and treated the popup's own
  # one-pixel border as text: every row of the box qualified, all four options merged into one
  # 91-row band (measured on the open combo). So the region is INSET past a vertical border, and
  # a band shorter than three rows is not text either -- the box's top and bottom rules are one
  # row each. The inset is a border rule, not a colour rule (sec.91's lesson): it says where the
  # geometry's frame can be, not what colour it is.
  #
  # Two blank rows split two bands. The second version tolerated a two-row gap and, with a value
  # already in the combo, the popup's SELECTED row (painted as a solid dark bar, rows 547-558 in
  # the kept picture) sat exactly two blank rows under the first row's glyphs (540-544) -- so
  # "the third band" was the fourth option. One row of slack rather than two, and the row
  # centres are the glyph bands, which is what a click on a row means.
  #
  # One dark pixel makes a row ink, not three. Asking for three lost the list box's second
  # option: at this zoom a 12 pt glyph's thin rows carry a single dark pixel (row 406 of "two" in
  # the kept picture, count profile 5 2 1 5 2), which split the band in half and left two
  # two-row fragments that the height rule below then dropped -- four rows on the box read as
  # two. What keeps the box's own rules out is the inset and the three-row height, not the count.
  $inset = 4
  $x0 = [int][math]::Max(0, [math]::Floor($m.bx0)) + $inset
  $x1 = [int][math]::Min($shot.width - 1, [math]::Ceiling($m.bx1)) - $inset
  $y0 = [int][math]::Max(0, [math]::Floor($m.by0)) + 2
  $y1 = [int][math]::Min($shot.height - 1, [math]::Ceiling($m.by1)) - 2
  $bands = @()
  $start = -1
  $last = -1
  for ($y = $y0; $y -le $y1; $y++) {
    $n = 0
    for ($x = $x0; $x -le $x1; $x++) {
      $o = $y * $shot.stride + $x * 4
      if ($shot.bytes[$o] -lt 150 -and $shot.bytes[$o + 1] -lt 150 -and $shot.bytes[$o + 2] -lt 150) { $n++ }
    }
    if ($n -ge 1) {
      if ($start -lt 0) { $start = $y }
      $last = $y
    } elseif ($start -ge 0 -and ($y - $last) -gt 1) {
      if (($last - $start) -ge 3) { $bands += @{ y0 = $start; y1 = $last; cy = ($start + $last) / 2 } }
      $start = -1
    }
  }
  if ($start -ge 0 -and ($last - $start) -ge 3) {
    $bands += @{ y0 = $start; y1 = $last; cy = ($start + $last) / 2 }
  }
  $parts = @()
  foreach ($b in $bands) { $parts += ('' + $b.y0 + '-' + $b.y1) }
  Say ('  ' + $tag + ': ' + $bands.Count + ' dark band(s) in rows ' + $y0 + '-' + $y1 + ' of columns ' +
       $x0 + '-' + $x1 + ' (inset ' + $inset + '): ' + ($parts -join ' '))
  return $bands
}

function Click-Blank([string]$tag) {
  # A click on paper that carries nothing, so a field loses the keyboard the way it does under a
  # reader's hand before they save. G1 measured that text still in a focused field stays out of the
  # saved bytes unless the focus is killed first; the product kills it on save, and this is the
  # belt to that brace.
  $shot = Page-Shot ($tag + '-blank')
  $m = Map-Rect $shot 450 780 460 790
  Assert-InCrop $shot $m ($tag + ' blank paper')
  [W32]::Click([int]$m.sx, [int]$m.sy)
  Start-Sleep -Milliseconds 500
}
function Type-Text([string]$text, [string]$tag) {
  foreach ($ch in $text.ToCharArray()) {
    $ins = [W32]::TypeChar($ch)
    if ($ins -ne 2) {
      Bail ('  ' + $tag + ': SendInput inserted ' + $ins + ' of 2 events for [' + (UForm ([string]$ch)) +
            '], last error ' + ([W32]::LastError()) + ' -- the key went nowhere, so nothing after it is evidence')
    }
    Start-Sleep -Milliseconds 60
  }
}
function Press-Key([int]$vk, [bool]$shift, [string]$tag) {
  $want = if ($shift) { 4 } else { 2 }
  # [uint16], not [ushort]: PowerShell has no accelerator by that name and the cast throws
  # TypeNotFound, which reads as "SendInput inserted  of 2 events" -- a key that never went out
  # looking like one that went out badly.
  $ins = [W32]::PressVk([uint16]$vk, $shift)
  if ($ins -ne $want) {
    Bail ('  ' + $tag + ': SendInput inserted ' + $ins + ' of ' + $want + ' events for vk ' + $vk +
          ', last error ' + ([W32]::LastError()) + ' -- the key never went out, so nothing after it is evidence')
  }
  Start-Sleep -Milliseconds 200
}
function Press-KeyAlt([int]$vk, [string]$tag) {
  # Alt held around one virtual key, for the one gesture H5 asks of the product by keyboard:
  # a combo box's list opens on Alt+Down (the backend turns that pair into a press on the
  # field's own dropdown button -- handoff-05 H1's measurement). Four events, and the count
  # is checked for the same reason Press-Key's is: a key that never went out must not read as
  # "Alt+Down does nothing".
  $ins = [W32]::PressAltVk([uint16]$vk)
  if ($ins -ne 4) {
    Bail ('  ' + $tag + ': SendInput inserted ' + $ins + ' of 4 events for Alt+vk ' + $vk +
          ', last error ' + ([W32]::LastError()) + ' -- the chord never went out, so nothing after it is evidence')
  }
  Start-Sleep -Milliseconds 200
}
function Save-AndRead([string]$tag, [bool]$press) {
  # Ctrl+S and the four receipts saveinplace already established: the line names the copy, the
  # caption's star is gone, the copy's bytes moved, the original's did not. The line is timed at
  # 5 s, so it is polled, never slept-once-and-read.
  if (-not (ForegroundIs (Window $app.Id))) {
    if (-not (TakeForeground $app.Id)) { Bail ('  ' + $tag + ': could not get the foreground back before Ctrl+S') }
  }
  $before = Copy-Hash
  if ($press) { [System.Windows.Forms.SendKeys]::SendWait('^s') }
  $savedCj = CjStr 0x5DF2, 0x4FDD, 0x5B58, 0x5230
  $msg = Wait-Status $app.Id @($savedCj, 'Saved to')
  $sawMsg = ($msg -ne '')
  $namesCopy = $msg.Contains($script:guardCopyLeaf)
  $starGone = Wait-Star $app.Id $false
  $after = Copy-Hash
  Say ('  ' + $tag + ': status [' + $msg.Replace("`r", ' ').Replace("`n", ' / ') + ']')
  Say ('  ' + $tag + ': save line=' + $sawMsg + ' names the copy=' + $namesCopy + ' star gone=' + $starGone +
       ' copy moved=' + ($after -ne $before))
  Say ('  ' + $tag + ': copy sha256 before [' + $before + ']')
  Say ('  ' + $tag + ': copy sha256 after  [' + $after + ']')
  # F8's in-place save reopens the document, so the view is rebuilt and the zoom is fit-width
  # again: anything aimed with the picture taken before the save would land somewhere else.
  $script:formPageRect = $null
  return @{ msg = $msg; sawMsg = $sawMsg; namesCopy = $namesCopy; starGone = $starGone;
            before = $before; after = $after; moved = ($after -ne $before) }
}
function Check-Save($sv, [string]$tag) {
  if (-not $sv.sawMsg) { Bail ('  ' + $tag + ': Ctrl+S left no save line in the status bar') }
  if (-not $sv.namesCopy) { Bail ('  ' + $tag + ': the save line does not name the copy [' + $script:guardCopyLeaf + ']') }
  if (-not $sv.starGone) { Bail ('  ' + $tag + ': the caption still carries the star after the save') }
  if (-not $sv.moved) { Bail ('  ' + $tag + ': the copy on disk is what it was, despite the line saying it saved') }
}
function Form-TitleCheck([string]$tag) {
  $win = TakeForeground $app.Id
  if (-not $win) { Bail ('  ' + $tag + ': could not take the foreground; refusing to type into another window') }
  $title = (Window $app.Id).Current.Name
  Say ('  ' + $tag + ': window [' + $title + ']')
  if (-not $title.Contains($script:guardCopyLeaf)) {
    Bail ('  ' + $tag + ': the caption does not name the copy this probe made [' + $script:guardCopyLeaf + ']')
  }
}

# ---- the signed modes' shared routes ------------------------------------------------------
# Two modes ask the same two questions: which bytes of this file are where, and does this field hold
# the keyboard. Both belong here once (sec.87), and both are arithmetic this harness has never done
# before -- the save modes compare a WHOLE-file hash, which says that something moved and nothing
# about what.
function File-Bytes([string]$path, [string]$tag) {
  # The bytes rather than the text: a hash of a decoded string would be a hash of whatever encoding
  # was picked. Test-Path first, for sec.83's reason -- a read of a path that is not there has to
  # say so here rather than arrive as an empty array that compares equal to another empty array.
  if (-not (Test-Path -LiteralPath $path)) { Bail ('  ' + $tag + ': [' + $path + '] is not on disk') }
  $bytes = [System.IO.File]::ReadAllBytes($path)
  Say ('  ' + $tag + ': ' + $bytes.Length + ' bytes read from [' + $path + ']')
  return ,$bytes
}
function Sha256-Of($bytes) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  return ([System.BitConverter]::ToString($sha.ComputeHash($bytes)) -replace '-', '')
}
function Sha256-Prefix($bytes, [int]$count, [string]$tag) {
  # The hash of the first $count bytes -- the shape of the question the incremental save answers.
  if ($count -lt 0 -or $count -gt $bytes.Length) {
    Bail ('  ' + $tag + ': asked for a prefix of ' + $count + ' bytes from a ' + $bytes.Length +
          '-byte file')
  }
  $head = New-Object 'byte[]' $count
  [Array]::Copy($bytes, 0, $head, 0, $count)
  return (Sha256-Of $head)
}
function Byte-Ranges($bytes, [string]$tag) {
  # Every /ByteRange in the file, as four numbers. Read off the bytes with a regex rather than
  # through PDFium: the question is what the FILE says, and the harness already trusts the library's
  # answer enough (pdfprobe prints the range it reads back, so a disagreement between the two shows
  # up in the sample lines rather than being hidden by a second opinion).
  #
  # The spans are searched in the whole buffer, including whatever an incremental update appends. A
  # rewrite that put the signature dictionary into a compressed object stream would therefore be
  # invisible to this read, which is why the count is printed and the mode insists on exactly one
  # match before the save: after it, the caller says how many are in front of it.
  $text = [System.Text.Encoding]::ASCII.GetString($bytes)
  $out = @()
  foreach ($m in [regex]::Matches($text, '/ByteRange\s*\[\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*\]')) {
    $out += ,@([long]$m.Groups[1].Value, [long]$m.Groups[2].Value,
               [long]$m.Groups[3].Value, [long]$m.Groups[4].Value)
  }
  $parts = @()
  foreach ($r in $out) { $parts += ('[' + ($r -join ' ') + ']') }
  Say ('  ' + $tag + ': ' + $out.Count + ' /ByteRange read(s): ' + ($parts -join ' '))
  return ,$out
}
function Covered-Bytes($bytes, $range, [string]$tag) {
  # The two spans the range names, concatenated -- PDF 32000-1 sec.12.8.1: everything except the
  # /Contents hole. Returns $null for a range that points off the end, and the caller says so rather
  # than hashing an empty array, which is sec.83's fake again.
  if (-not $range -or $range.Count -ne 4) { return $null }
  $list = New-Object System.Collections.Generic.List[byte]
  for ($p = 0; $p -lt 2; $p++) {
    $off = [int]$range[$p * 2]
    $len = [int]$range[$p * 2 + 1]
    if ($off -lt 0 -or $len -lt 0 -or ($off + $len) -gt $bytes.Length) {
      Say ('  ' + $tag + ': span ' + $off + '+' + $len + ' does not fit in ' + $bytes.Length + ' bytes')
      return $null
    }
    for ($k = 0; $k -lt $len; $k++) { $list.Add($bytes[$off + $k]) }
  }
  return ,$list.ToArray()
}
function Hex16([string]$h) {
  # A hash printed short. Two of these land on one line in a log a person reads, and sec.83's rule
  # still holds at the same time: the line carries the byte count next to it, because a comparison of
  # two empty strings is not a comparison of two files.
  if ($h.Length -le 16) { return $h }
  return ($h.Substring(0, 16) + '...')
}
function Says-Any([string]$text, [string[]]$needles) {
  # Both spellings of one sentence, because every word here comes from the catalogue and a match list
  # written for one locale has been the death of more than one round (sec.84, sec.87).
  foreach ($n in $needles) { if ($text -like ('*' + $n + '*')) { return $true } }
  return $false
}
function Signed-Needles() {
  # The four sentences a signed document puts on screen, each with both spellings, in one place --
  # the two modes read all four and a second copy of a needle list is a list that only gets run in
  # one language. The Chinese halves are built from code points because this file has to stay ASCII.
  #
  # P=1 and P>=2 are deliberately told apart by the fragment that differs (the reader is told either
  # that nothing may change or that only form filling may), and each fragment is checked against the
  # OTHER one's absence as well: the two sentences contradict each other, so a document that shows
  # both is showing a notice that was not read off /P.
  #
  # The English halves are read out of Resources/translations/xyzpdf_en.ts rather than typed from
  # what a translation would probably say: 'will invalidate' is what the singular and the plural of
  # sec.122's sentence share, and 'permits form filling' is what the status line and the delete box
  # each say in their own words.
  return @{
    # sec.126 (handoff-07 I1): the open line says the signatures are being checked, and a moment
    # later the verdict replaces it. Both carry the count, so 'signed' matches either; 'checking' is
    # only in the open line, which is the one that names the file.
    signed = @((CjStr 0x4E2A, 0x6570, 0x5B57, 0x7B7E, 0x540D), 'digital signature')
    checking = @((CjStr 0x6B63, 0x5728, 0x68C0, 0x67E5), 'being checked')
    certP1 = @((CjStr 0x4E0D, 0x5141, 0x8BB8, 0x4EFB, 0x4F55, 0x6539, 0x52A8),
               'forbids every change')
    certP2 = @((CjStr 0x53EA, 0x5141, 0x8BB8, 0x586B, 0x5199, 0x8868, 0x5355),
               'permits form filling')
    invalidate = @((CjStr 0x6570, 0x5B57, 0x7B7E, 0x540D, 0x90FD, 0x4F1A, 0x5931, 0x6548),
                   'will invalidate')
  }
}
function SignatureNeedles() {
  # The sidebar's own labels and the four verdicts, both spellings of each, in one place. A second
  # copy of this table is how one mode ends up readable in only one language (sec.87), and the
  # Chinese halves have to be built from code points because this file must stay pure ASCII.
  return @{
    tab = @('Signatures', (CjStr 0x7B7E, 0x540D))
    cert = @('Certificate not trusted or expired',
             (CjStr 0x8BC1, 0x4E66, 0x4E0D, 0x53D7, 0x4FE1, 0x4EFB, 0x6216, 0x5DF2, 0x8FC7, 0x671F))
    changed = @('Content changed', (CjStr 0x5185, 0x5BB9, 0x88AB, 0x6539, 0x8FC7))
    cannot = @('Cannot be checked', (CjStr 0x65E0, 0x6CD5, 0x9A8C, 0x8BC1))
    inc1 = @('There is 1 incremental update after the signature',
             ((CjStr 0x7B7E, 0x540D, 0x4E4B, 0x540E, 0x8FD8, 0x6709) + ' 1 ' +
              (CjStr 0x6B21, 0x589E, 0x91CF, 0x66F4, 0x65B0)))
    inc2 = @('There are 2 incremental updates after the signature',
             ((CjStr 0x7B7E, 0x540D, 0x4E4B, 0x540E, 0x8FD8, 0x6709) + ' 2 ' +
              (CjStr 0x6B21, 0x589E, 0x91CF, 0x66F4, 0x65B0)))
    incAny = @('incremental update after the signature',
               (CjStr 0x7B7E, 0x540D, 0x4E4B, 0x540E, 0x8FD8, 0x6709))
  }
}
function Pick-Needles($pair, [string]$lang) {
  # A pair is @(english, chinese). A round that names its interface gets only that spelling, which
  # is what makes an English installed run prove the English label was on screen instead of the
  # Chinese one this file also knows how to match.
  if ($lang -eq 'en') { return @($pair[0]) }
  if ($lang -eq 'zh') { return @($pair[1]) }
  return @($pair)
}
function InterfaceLanguage([int]$procId) {
  # The menu bar's top-level words are tr()'d, so one membership test names the interface without
  # reading a setting -- and the packaged copy's setting has no outside read path (sec.48). The
  # names carry their mnemonic the way the source spells it: the Chinese bar reads the word for
  # "file" followed by "(F)" and the English one "File" (the in-word & is not in the UIA name), so
  # the match is a PREFIX on "File" and on the Chinese word rather than an exact name.
  $zh = CjStr 0x6587, 0x4EF6
  foreach ($n in (MenuBarNames $procId)) {
    if ($n -like 'File*') { return 'en' }
    if ($n -like ($zh + '*')) { return 'zh' }
  }
  return 'unknown'
}
function Check-EnglishSentences([string]$text, [string]$tag, [bool]$certificationWanted) {
  # sec.126 items 1 and 2, read off the window rather than trusted to the catalogue: two sentences
  # on the status bar are joined with a space in English, and the punctuation between counted
  # fragments is the language's. "signatures.Its certification" and a full-width comma are what the
  # bar said before the fix; both are checked here because a catalogue rule cannot see the line.
  $ok = $true
  if ($text -match '[.:][A-Z]') {
    Say ('  ' + $tag + ': two English sentences are welded together in |' + $text + '|')
    $ok = $false
  }
  $bad = @(0xFF0C, 0x3002, 0xFF1A, 0xFF1B, 0xFF01, 0xFF08, 0xFF09, 0x3001, 0xFF1F)
  foreach ($cp in $bad) {
    if ($text.Contains([string][char]$cp)) {
      Say ('  ' + $tag + ': full-width punctuation U+' + $cp.ToString('X4') + ' in |' + $text + '|')
      $ok = $false
    }
  }
  if ($certificationWanted -and -not $text.Contains(' Its certification')) {
    Say ('  ' + $tag + ': the certification sentence does not start with its own space in |' +
         $text + '|')
    $ok = $false
  }
  return $ok
}
function SidebarTabNames() {
  # Every label the reading sidebar's four tabs can show. A document tab is a file name and none of
  # these, which is how the sidebar's strip is told from the window's own tab strip.
  return @('Outline', 'Thumbnails', 'Search', 'Signatures',
           (CjStr 0x5927, 0x7EB2), (CjStr 0x7F29, 0x7565, 0x56FE),
           (CjStr 0x641C, 0x7D22), (CjStr 0x7B7E, 0x540D))
}
function SidebarUpLoose($tabs) {
  $names = SidebarTabNames
  foreach ($t in $tabs) { if ($names -contains $t.Current.Name) { return $true } }
  return $false
}
function SidebarComplete($tabs, [string]$lang) {
  # The three tabs every document has. Requiring them before judging the signatures tab's absence
  # is what keeps the negative half from passing on a sidebar that never opened -- a receipt that
  # was true before the interaction is not a receipt (sec.97).
  $sets = @()
  if ($lang -ne 'zh') { $sets += ,@('Outline', 'Thumbnails', 'Search') }
  if ($lang -ne 'en') { $sets += ,@((CjStr 0x5927, 0x7EB2), (CjStr 0x7F29, 0x7565, 0x56FE), (CjStr 0x641C, 0x7D22)) }
  foreach ($set in $sets) {
    $have = 0
    foreach ($n in $set) {
      foreach ($t in $tabs) { if ($t.Current.Name -eq $n) { $have++; break } }
    }
    if ($have -eq $set.Count) { return $true }
  }
  return $false
}
function Find-TabByName($tabs, [string[]]$names) {
  foreach ($t in $tabs) {
    foreach ($n in $names) { if ($t.Current.Name -eq $n) { return $t } }
  }
  return $null
}
function TabSelected($tab) {
  # $null means the pattern is not offered, which is a different answer from "not selected" and is
  # printed as such: a mode that conflated the two would call a missing pattern a product defect.
  $sp = Pat $tab 'SelectionItem'
  if (-not $sp) { return $null }
  try { return [bool]$sp.Current.IsSelected } catch { return $null }
}
function TabOnScreen($tab) {
  # A hidden QTabBar page is still enumerated by this Qt build's UIA provider: the signed panel and
  # the hidden signatures tab both answer FindAll. "The page is not there" is a statement about the
  # screen, so presence in the tree is not enough -- offscreen or a zero rectangle is not a tab the
  # reader can see or press.
  if (-not $tab) { return $false }
  try {
    if ($tab.Current.IsOffscreen) { return $false }
    $r = $tab.Current.BoundingRectangle
    return ($r.Width -gt 0 -and $r.Height -gt 0)
  } catch { return $false }
}
function SignatureExpectation([string]$sample) {
  # What the panel must say, from the verdict order in signatureverify.cpp: the document's own bytes
  # coming apart is reported before any question about who signed it, so a tampered file is
  # ContentModified even though its certificate is untrusted too. The trust half is this machine's:
  # the sample's CA is not in the machine's root store, so every otherwise-valid sample is a
  # certificate problem here. Rows is the top-level count; inc is the revision sentence's number.
  switch ($sample) {
    'valid'            { return @{ verdict = 'cert';    rows = 1; inc = 0 } }
    'expired'          { return @{ verdict = 'cert';    rows = 1; inc = 0 } }
    'appended-one'     { return @{ verdict = 'cert';    rows = 1; inc = 1 } }
    'appended-two'     { return @{ verdict = 'cert';    rows = 1; inc = 2 } }
    'tampered'         { return @{ verdict = 'changed'; rows = 1; inc = 0 } }
    'narrowed'         { return @{ verdict = 'changed'; rows = 1; inc = 0 } }
    'x509-rsa-sha1'    { return @{ verdict = 'cannot';  rows = 1; inc = 0 } }
    'broken-blob'      { return @{ verdict = 'cannot';  rows = 1; inc = 0 } }
    'gap-at-front'     { return @{ verdict = 'cert';    rows = 1; inc = 0 } }
    'token-unreadable' { return @{ verdict = 'cert';    rows = 1; inc = 0 } }
  }
  return $null
}
function Dump-Names($root, [string]$tag) {
  # "Nothing found" has three causes in this window -- the sidebar never opened, the page is not
  # current, and the panel is genuinely empty -- and one line cannot tell them apart. Printing
  # every distinct class+name is what turns a dead round into a reading (sec.87's rule).
  Say ('  ' + $tag + ':')
  $seen = @{}
  foreach ($e in @($root.FindAll($TS::Descendants, $ANY))) {
    $nm = $e.Current.Name
    if ($null -eq $nm) { continue }
    $nm = $nm.Replace("`r", ' ').Replace("`n", ' ').Trim()
    if ($nm.Length -lt 2) { continue }
    $key = $e.Current.ControlType.ProgrammaticName.Replace('ControlType.', '') + ' |' + $nm + '|'
    if ($seen.ContainsKey($key)) { continue }
    $seen[$key] = 1
    Say ('    ' + $key)
  }
}
function Save-WidgetPicture([IntPtr]$hwnd, [string]$name, $elements, [bool]$wrongAnchor) {
  # One PrintWindow of the whole window, then a crop of the elements handed in -- the tab strip and
  # the panel, so the picture shows which page is current and what it says. The crop is anchored to
  # the window's FRAME and clamped into the client area, both geometric assertions borrowed from
  # sec.89/sec.91: UIA's rectangle is the client area while PrintWindow paints the whole window, and
  # a crop computed with the wrong one lands on the frame or above the strip while every count stays
  # green. -Control passes $wrongAnchor to repeat that mistake on purpose.
  $gr = [W32]::Rect($hwnd)
  $co = [W32]::ClientOrigin($hwnd)
  $fw = [int]($gr.right - $gr.left)
  $fh = [int]($gr.bottom - $gr.top)
  if ($fw -le 0 -or $fh -le 0) { Bail '  the window has no frame to paint into' }
  $winBmp = New-Object System.Drawing.Bitmap($fw, $fh)
  $wg = [System.Drawing.Graphics]::FromImage($winBmp)
  $hdc = $wg.GetHdc()
  $painted = [W32]::PrintWindow($hwnd, $hdc, 2)
  $wg.ReleaseHdc($hdc)
  $winShot = Join-Path $scratch ('signaturepanel-win-' + $name + '.png')
  $winBmp.Save($winShot, [System.Drawing.Imaging.ImageFormat]::Png)
  $wg.Dispose()
  Say ('  PrintWindow returned ' + $painted + ', window ' + $fw + 'x' + $fh + ' kept at ' + $winShot +
       ' (' + (Get-Item $winShot).Length + ' bytes, sha256 ' +
       (Get-FileHash -LiteralPath $winShot -Algorithm SHA256).Hash.Substring(0, 16) + '...)')

  $minX = [double]::MaxValue
  $minY = [double]::MaxValue
  $maxX = [double]::MinValue
  $maxY = [double]::MinValue
  $count = 0
  foreach ($e in $elements) {
    if ($null -eq $e) { continue }
    $r = $e.Current.BoundingRectangle
    if ($r.Width -le 0 -or $r.Height -le 0) { continue }
    $count++
    if ($r.X -lt $minX) { $minX = $r.X }
    if ($r.Y -lt $minY) { $minY = $r.Y }
    if (($r.X + $r.Width) -gt $maxX) { $maxX = $r.X + $r.Width }
    if (($r.Y + $r.Height) -gt $maxY) { $maxY = $r.Y + $r.Height }
  }
  if ($count -eq 0) {
    $winBmp.Dispose()
    Bail ('  no element with a rectangle to crop for [' + $name + ']')
  }
  $anchorX = $gr.left
  $anchorY = $gr.top
  if ($wrongAnchor) {
    # the sec.89 mistake: the picture is anchored to the frame, UIA's rectangle is the client area
    $anchorX = $co.x
    $anchorY = $co.y
  }
  $left = [int][math]::Floor($minX - $anchorX - 8)
  $top = [int][math]::Floor($minY - $anchorY - 8)
  $width = [int][math]::Ceiling($maxX - $minX + 16)
  $height = [int][math]::Ceiling($maxY - $minY + 16)
  if ($left -lt 0) { $left = 0 }
  if ($top -lt 0) { $top = 0 }
  if ($left + $width -gt $fw) { $width = $fw - $left }
  if ($top + $height -gt $fh) { $height = $fh - $top }
  $cl = $co.x - $gr.left
  $ct = $co.y - $gr.top
  if ($left -lt $cl) { $left = $cl }
  if ($top -lt $ct) { $top = $ct }
  if ($left + $width -gt $fw) { $width = $fw - $left }
  if ($top + $height -gt $fh) { $height = $fh - $top }
  if ($width -le 0 -or $height -le 0) {
    $winBmp.Dispose()
    Bail ('  the crop collapsed for [' + $name + '] (' + $width + 'x' + $height + ')')
  }
  $outside = @()
  foreach ($e in $elements) {
    if ($null -eq $e) { continue }
    $r = $e.Current.BoundingRectangle
    if ($r.Width -le 0 -or $r.Height -le 0) { continue }
    $bx = [int]($r.X - $gr.left)
    $by = [int]($r.Y - $gr.top)
    if ($bx -lt $left -or $by -lt $top -or
        ($bx + [int]$r.Width) -gt ($left + $width) -or
        ($by + [int]$r.Height) -gt ($top + $height)) {
      $outside += (($e.Current.ControlType.ProgrammaticName -replace 'ControlType\.', '') + ' |' +
                   $e.Current.Name + '| maps to ' + $bx + ',' + $by + ' ' +
                   [int]$r.Width + 'x' + [int]$r.Height)
    }
  }
  if ($outside.Count -gt 0) {
    foreach ($l in $outside) { Say ('  outside the crop: ' + $l) }
    Say ('  crop ' + $left + ',' + $top + ' ' + $width + 'x' + $height + ' anchored at ' +
         $anchorX + ',' + $anchorY + ' (' + $(if ($wrongAnchor) { 'CLIENT' } else { 'FRAME' }) + ')')
    $winBmp.Dispose()
    Bail ('  the sidebar does not fit inside the crop, so the picture is not of it' +
          $(if ($wrongAnchor) { ' (CONTROL exit 1)' } else { '' }))
  }
  $cropRect = New-Object System.Drawing.Rectangle($left, $top, $width, $height)
  $bmp = $winBmp.Clone($cropRect, $winBmp.PixelFormat)
  $shot = Join-Path $scratch ('signaturepanel-' + $name + '.png')
  $bmp.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  $winBmp.Dispose()
  Say ('  crop ' + $width + 'x' + $height + ' at window-local ' + $left + ',' + $top +
       ' kept at ' + $shot + ' (' + (Get-Item $shot).Length + ' bytes, sha256 ' +
       (Get-FileHash -LiteralPath $shot -Algorithm SHA256).Hash.Substring(0, 16) + '...)')
}
function Focus-Delta([string]$tag, $f) {
  # Does this field hold the keyboard -- as a CHANGE inside its own rectangle, not a count of it.
  #
  # The form modes can ask "which field's rectangle holds 24 px of caret" because their sample's
  # fields start EMPTY (Caret-Map's own LIMIT paragraph says the count only answers while they are).
  # The signed sample cannot be asked that: its field carries /V (before) from the first byte, so its
  # rectangle already holds glyphs and an absolute count answers "the caret is here" for a field
  # nobody touched -- a receipt that was impossible to fail before the interaction even started
  # (sec.97's rule). The reading that survives is the difference between the same rectangle with the
  # keyboard and without it, so the field is first made to let go (a click on blank paper), shot
  # twice a blink apart, then clicked and shot twice again. The larger of each pair is compared,
  # because a caret is allowed to be in its dark phase (sec.120 measured 0 then 24).
  #
  # The bar is 10 px for the same reason Caret-Holders uses it: a caret is 24 and a field that did
  # not take the keyboard changes by 0.
  Click-Blank ($tag + '-unfocused')
  $s1 = Page-Shot ($tag + '-out-1')
  $a = Region-Ink $s1 (Map-Rect $s1 $f.l $f.b $f.r $f.t)
  Start-Sleep -Milliseconds 700
  $s2 = Page-Shot ($tag + '-out-2')
  $b = Region-Ink $s2 (Map-Rect $s2 $f.l $f.b $f.r $f.t)
  $before = [math]::Max($a, $b)
  Click-Field $s2 $f $tag
  $s3 = Page-Shot ($tag + '-in-1')
  $c = Region-Ink $s3 (Map-Rect $s3 $f.l $f.b $f.r $f.t)
  Start-Sleep -Milliseconds 700
  $s4 = Page-Shot ($tag + '-in-2')
  $d = Region-Ink $s4 (Map-Rect $s4 $f.l $f.b $f.r $f.t)
  $after = [math]::Max($c, $d)
  Say ('  ' + $tag + ': ink in [' + $f.name + '] -- without the keyboard ' + $a + '/' + $b + ' (take ' +
       $before + '), with it ' + $c + '/' + $d + ' (take ' + $after + '), delta ' + ($after - $before))
  return @{ before = $before; after = $after; delta = ($after - $before) }
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
  # The picker route clicked the File menu, so the pointer is left resting over the menu bar and
  # toolbar: a status tip would then sit on top of the open-time notice for as long as it stays
  # there (sec.122 read the select tool's tip instead of the signature sentence). Parked the same
  # way the launch parks it; the notice comes back with the time it had left (sec.114).
  ParkCursorAway
}

function Export-Round([string]$kind) {
  # File > Export to Word / Excel / PowerPoint, and Make Scanned Pages Searchable, on the screen:
  # the menu row, the Excel options box (Excel only), the native save box, the background job, the
  # box that says it is done -- and the file itself, checked by a reader that is not this program's
  # own (check_docx/check_xlsx/check_pptx.py: zipfile and ElementTree; the searchable PDF by
  # pdfprobe --text, i.e. PDFium's text page) and for the page's words. sec.124 rebuilt the Word
  # conversion and sec.125 left it as the one link never pressed on the installed copy; handoff-09
  # added the other three and said the main session gives each a round the same shape. The output
  # goes into the probe's own directory; nothing here writes beside the document, and the searchable
  # round hashes -Doc before and after because that job READS a PDF to write a PDF. (Not $target:
  # PowerShell names are case-blind, and that one is the -Target parameter this mode reads.)
  # -Control presses nothing and keeps every verdict: no box, no file, exit 1.
  $spec = switch ($kind) {
    'word'       { @{ ext = 'docx'; row = @('Export to Word', ((CjStr 0x5BFC, 0x51FA, 0x4E3A) + ' Word'));
                      checker = 'check_docx.py'; part = 'word/document.xml'; run = '<w:t[ >]' } }
    'excel'      { @{ ext = 'xlsx'; row = @('Export to Excel', ((CjStr 0x5BFC, 0x51FA, 0x4E3A) + ' Excel'));
                      checker = 'check_xlsx.py'; part = 'xl/sharedStrings.xml'; run = '<t[ >]' } }
    'ppt'        { @{ ext = 'pptx'; row = @('Export to PowerPoint', ((CjStr 0x5BFC, 0x51FA, 0x4E3A) + ' PowerPoint'));
                      checker = 'check_pptx.py'; part = 'ppt/slides/slide1.xml'; run = '<a:t[ >]' } }
    'searchable' { @{ ext = 'pdf'; row = @('Make Scanned Pages Searchable',
                                            ((CjStr 0x626B, 0x63CF, 0x4EF6, 0x8F6C, 0x6210, 0x53EF, 0x641C, 0x7D22, 0x7684) + ' PDF'));
                      checker = ''; part = ''; run = '' } }
  }
  $label = $spec.row[0]
  $docHash = ''
  if ($kind -eq 'searchable' -and (Test-Path -LiteralPath $Doc)) {
    $docHash = (Get-FileHash -LiteralPath $Doc -Algorithm SHA256).Hash
    Say ('  -Doc sha256 before ' + $docHash.Substring(0, 16) + '... [' + $Doc + ']')
  }
  $outDir = Join-Path $scratch ($Mode + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
  New-Item -ItemType Directory -Path $outDir | Out-Null
  $outFile = Join-Path $outDir ('exported.' + $spec.ext)
  Say ('  the output will be written to [' + $outFile + ']')
  if (-not (TakeForeground $app.Id)) { Bail '  could not take the foreground' }
  $sv = $null
  if ($Control) {
    Say ('  CONTROL: the ' + $label + ' row is not pressed; every verdict stays')
  } else {
    for ($try = 1; $try -le 3 -and -not $sv; $try++) {
      TopMenu $app.Id @('File', (CjStr 0x6587, 0x4EF6)) | Out-Null
      $row = RowAny $app.Id $spec.row
      if (-not $row) { Say ('  try ' + $try + ': no ' + $label + ' row under File'); Start-Sleep -Milliseconds 600; continue }
      Say ('  try ' + $try + ': menu row |' + $row.Current.Name + '| enabled=' + $row.Current.IsEnabled)
      ClickAt $row
      if ($kind -eq 'excel') {
        # The one choice the Excel export asks before the save box (src/ui/excelexportdialog.cpp),
        # found by class because its title is translated; its accept button is Export.
        $opt = $null
        for ($k = 1; $k -le 10 -and -not $opt; $k++) {
          Start-Sleep -Milliseconds 400
          $opt = FindClass $app.Id 'ExcelExportDialog'
        }
        if (-not $opt) { Say ('  try ' + $try + ': no ExcelExportDialog after the row'); continue }
        Say ('  options box |' + $opt.Current.Name + '|')
        foreach ($c in @(FindType $opt $CT::CheckBox)) {
          $tg = Pat $c 'Toggle'
          Say ('    check |' + $c.Current.Name + '| state=' + $(if ($tg) { $tg.Current.ToggleState } else { '?' }))
        }
        $accept = MatchOne $opt $CT::Button @('Export', (CjStr 0x5BFC, 0x51FA))
        if (-not $accept) { Bail '  the Excel options box has no Export button' }
        ClickAt $accept
      }
      for ($k = 1; $k -le 10 -and -not $sv; $k++) {
        Start-Sleep -Milliseconds 500
        $sv = FindPicker $app.Id
      }
    }
    if (-not $sv) { Bail ('  no native save box after three tries at the ' + $label + ' row') }
    Say ('  save box [' + $sv.Current.Name + '] class=' + $sv.Current.ClassName)
    $kids = PickerChildren $sv
    $go = $null
    foreach ($h in $kids) {
      if ([W32]::Cls($h) -eq 'Button' -and [W32]::GetDlgCtrlID($h) -eq 1) { $go = $h }
    }
    $nameBox = NameField $kids
    if (-not ($nameBox -and $go)) { Bail '  no file-name field / no Button id=1 in the save box' }
    [W32]::SendMessageW($nameBox, 0x000C, [IntPtr]0, $outFile) | Out-Null
    Start-Sleep -Milliseconds 700
    # A real click on the drawn button: the save box does not answer BM_CLICK (sec.44).
    $gr = [W32]::Rect([IntPtr]$go)
    [W32]::Click([int](($gr.left + $gr.right) / 2), [int](($gr.top + $gr.bottom) / 2))
  }

  # ---- the verdicts, the same in both runs ----
  # Done is the box that says so (a QMessageBox, found by class: its title is translated). Recognition
  # renders every scanned page at 300 dpi, so the searchable round waits longer.
  $exists = $false
  $box = $null
  $rounds = $(if ($kind -eq 'searchable') { 360 } else { 120 })
  for ($k = 1; $k -le $rounds -and -not $box; $k++) {
    Start-Sleep -Milliseconds 500
    $exists = Test-Path -LiteralPath $outFile
    if ($exists) { $box = FindClass $app.Id 'QMessageBox' }
    if (-not $exists -and $k -ge 24 -and $Control) { break }
  }
  $size = 0
  if ($exists) { $size = (Get-Item -LiteralPath $outFile).Length }
  Say ('  target exists=' + $exists + '  size=' + $size + '  done box=' + ($null -ne $box))
  if ($docHash) {
    $after = (Get-FileHash -LiteralPath $Doc -Algorithm SHA256).Hash
    Say ('  -Doc sha256 after  ' + $after.Substring(0, 16) + '...')
    if ($after -ne $docHash) { Bail '  THE DOCUMENT THAT WAS READ CHANGED -- the job wrote the user''s PDF' }
  }
  if (-not $exists) {
    if ($Control) { Bail '  CONTROL: with nothing pressed no file came out -- the verdict is live (CONTROL exit 1)' }
    $any = FindClass $app.Id 'QMessageBox'
    if ($any) { foreach ($t in @(FindType $any $CT::Text)) { Say ('    box text |' + $t.Current.Name.Replace("`n", ' / ') + '|') } }
    Bail ('  the save box was accepted but no file came out at [' + $outFile + ']')
  }
  if ($box) {
    Say ('  done box |' + $box.Current.Name + '|')
    foreach ($t in @(FindType $box $CT::Text)) { Say ('    text |' + $t.Current.Name.Replace("`n", ' / ') + '|') }
    $ok = MatchOne $box $CT::Button @('OK', (CjStr 0x786E, 0x5B9A))
    if ($ok) { ClickAt $ok; Start-Sleep -Milliseconds 600 }
  } else {
    Bail '  the file came out but the box that says the job is done never did'
  }

  if ($kind -eq 'searchable') {
    # PDFium's own text page, through the pdfprobe beside -Exe: characters it can read, and how many
    # of them are drawn invisibly -- a text layer that is visible, or one PDFium does not take in,
    # both fail here.
    $tool = Probe-Tool $Mode
    $read = @(& $tool --text $outFile 2>&1)
    $readExit = $LASTEXITCODE
    foreach ($l in $read) { Say ('  pdfprobe: ' + $l) }
    if ($readExit -ne 0) { Bail ('  pdfprobe --text exited ' + $readExit + ' on the output') }
    $tot = @($read | Where-Object { $_ -like 'chars:*' } | Select-Object -First 1)
    $invisible = 0
    if ($tot.Count -gt 0 -and $tot[0] -match 'invisible: (\d+)') { $invisible = [int]$Matches[1] }
    if ($invisible -le 0) { Bail '  the output carries no invisible text -- the layer is not there' }
    if ($Target -and -not (@($read) -join ' ').Contains($Target)) { Bail ('  the output does not carry [' + $Target + ']') }
    Say ('  the searchable PDF came out through the screen with ' + $invisible + ' invisible character(s), and -Doc is unchanged')
    return
  }

  $checker = Join-Path $PSScriptRoot $spec.checker
  $checked = & python $checker $outFile 2>&1
  $checkExit = $LASTEXITCODE
  foreach ($l in @($checked | Select-Object -Last 3)) { Say ('  ' + $spec.checker + ': ' + $l) }
  if ($checkExit -ne 0) { Bail ('  ' + $spec.checker + ' exited ' + $checkExit + ' on the export') }
  # Words that came off the page: the part named in the spec has text runs in it, and the page's own
  # first characters among them when the caller says what they are (-Target).
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $zip = [System.IO.Compression.ZipFile]::OpenRead($outFile)
  try {
    $entry = $zip.GetEntry($spec.part)
    if (-not $entry) { Bail ('  the export has no ' + $spec.part) }
    $reader = New-Object System.IO.StreamReader($entry.Open(), [System.Text.Encoding]::UTF8)
    $xml = $reader.ReadToEnd()
    $reader.Close()
  } finally { $zip.Dispose() }
  $runs = ([regex]::Matches($xml, $spec.run)).Count
  Say ('  ' + $spec.part + ': ' + $xml.Length + ' characters, ' + $runs + ' text runs')
  if ($runs -eq 0) { Bail '  the export has no text in it' }
  if ($Target -and -not $xml.Contains($Target)) { Bail ('  the export does not carry [' + $Target + ']') }
  Say '  exported through the screen, the file checks out, and the words are in it'
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
  'closeprompt' {
    # The other half of F8 that has never been on a screen (sec.116): closing a window whose tab
    # still holds unwritten edits asks for one of three answers -- Save, Discard, Cancel.
    # MainWindow::confirmDiscardingEdits() asks through WindowDialogs::question, which is a
    # QMessageBox, so the box is found by that class name and not by its title: the title and the
    # body are our own translatable strings, and the button texts come from Qt's own catalogue --
    # loaded for the Chinese interface and deliberately not for the English one -- so neither
    # language's wording can be assumed here, and both are asked for.
    #
    # Three rounds, one per answer, each with its own receipt on disk:
    #   Cancel   -- the box goes away, the window stays, the star stays, the copy is untouched;
    #   Discard  -- the window goes away, the copy is untouched;
    #   Save     -- a fresh launch (the window is gone after Discard), the same edit, and this time
    #               the window goes away AND the copy moves, with /Rotate 90 really in its bytes.
    # -Control keeps the same verdict (a box must appear when the window closes) and leaves the one
    # thing that makes it appear undone: no edit at all. That must exit 1.
    # Measured 2026-10-23 rather than guessed: the Chinese interface takes these three from Qt's own
    # catalogue as U+4FDD U+5B58 / U+53D6 U+6D88 / U+4E22 U+5F03 -- the third is 'discard' in the
    # sense of throwing away, which is neither of the two Chinese words (U+653E U+5F03, U+4E0D U+4FDD
    # U+5B58) this needle list carried before it was run -- and the English one as |Save| |Cancel|
    # |Discard|, in that order.
    $answerSave = @('Save', (CjStr 0x4FDD, 0x5B58))
    # sec.118: the product now labels this button itself -- U+4E0D U+4FDD U+5B58 '(N)' / "Don't Save".
    $answerDiscard = @("Don't Save", ((CjStr 0x4E0D, 0x4FDD, 0x5B58) + '(N)'), (CjStr 0x4E0D, 0x4FDD, 0x5B58),
                       'Discard', (CjStr 0x4E22, 0x5F03))
    $answerCancel = @('Cancel', (CjStr 0x53D6, 0x6D88))

    function Prompt-Three([int]$procId, [string]$tag) {
      # The box, judged as three answers about THIS document. Buttons are printed by PressBoxButton
      # when one of them is pressed; the count is judged here because a box that grew or lost one is
      # a different question than the one this mode is written against.
      $box = Wait-PromptBox $procId $tag
      if (-not $box) {
        PrintCandidates $procId ($tag + ': no QMessageBox; every candidate window of the process:')
        Bail ('  ' + $tag + ': closing an edited document produced no three-answer box')
      }
      $buttons = @(FindType $box $CT::Button)
      Say ('  ' + $tag + ': ' + $buttons.Count + ' Button element(s) in the box')
      if ($buttons.Count -ne 3) {
        foreach ($b in $buttons) { Say ('    |' + $b.Current.Name + '|') }
        Bail ('  ' + $tag + ': the box has ' + $buttons.Count + ' buttons, not the three answers')
      }
      # The question has to name the file this probe made: a box about somebody else's document would
      # be a window that was already on screen, and reading it as this run's receipt is exactly the
      # stale-handle shape sec.87 wrote down.
      $body = LongestText $box
      Say ('  ' + $tag + ': body |' + $body.Replace("`r", ' ').Replace("`n", ' / ') + '|')
      if (-not $body.Contains($script:guardCopyLeaf)) {
        Bail ('  ' + $tag + ': the box does not name the copy this probe made [' + $script:guardCopyLeaf + ']')
      }
      return $box
    }

    if ($Control) {
      Say '  CONTROL: nothing is edited before the close, but the same box verdict stays in place'
      if (-not (Close-Window $app.Id)) { Bail '  no window to close' }
      $cbox = Wait-PromptBox $app.Id 'control'
      if ($cbox) {
        Bail '  CONTROL: the window asked about unwritten changes although nothing was edited -- the question is not conditional'
      }
      Bail '  CONTROL: an untouched document closed without a box -- the box verdict is live (CONTROL exit 1)'
    }

    # ---- round 1: Cancel keeps everything ----
    Rotate-CurrentPage $app.Id
    if (-not (Wait-Star $app.Id $true)) {
      Bail ('  the caption never grew the unsaved-edits star; it reads [' + (Window $app.Id).Current.Name + ']')
    }
    $hashOpen = Copy-Hash
    Say ('  caption [' + (Window $app.Id).Current.Name + ']  copy [' + $hashOpen + ']')
    if (-not (Close-Window $app.Id)) { Bail '  no window to close' }
    $box1 = Prompt-Three $app.Id 'cancel'
    if (-not (PressBoxButton $box1 $answerCancel 'cancel')) { Bail '  the box has no Cancel button in either language' }
    if (-not (WaitClassGone $app.Id 'QMessageBox' 15)) { Bail '  the box stayed open after Cancel' }
    if (-not (Window $app.Id)) { Bail '  Cancel closed the window; the reader kept neither the tab nor the edits' }
    if (-not (Has-Star $app.Id)) { Bail '  Cancel let the star go, so the tab now claims nothing is unwritten' }
    if ((Copy-Hash) -ne $hashOpen) { Bail '  Cancel wrote the file -- it is not the answer that changes nothing' }
    Say ('  Cancel: window still here [' + (Window $app.Id).Current.Name + '], copy still [' + (Copy-Hash) + ']')

    # ---- round 2: Discard closes without writing ----
    if (-not (Close-Window $app.Id)) { Bail '  no window to close the second time' }
    $box2 = Prompt-Three $app.Id 'discard'
    if (-not (PressBoxButton $box2 $answerDiscard 'discard')) { Bail '  the box has no Discard button in either language' }
    if (-not (Wait-WindowGone $app.Id 30)) { Bail '  the window stayed open after the Discard answer' }
    if ((Copy-Hash) -ne $hashOpen) { Bail '  Discard wrote the file -- it is the answer that throws the edits away' }
    Say ('  Discard: the window is gone and the copy is still [' + (Copy-Hash) + '] -- nothing was written')

    # ---- round 3: Save writes, then closes ----
    # A fresh instance: the window is gone, and the copy on disk is still the bytes it was opened with.
    $app3 = Restart-ProbeApp $Language
    if (-not $app3) { Bail '  the relaunch produced no window for the Save round' }
    Say ('  relaunched [' + (Window $app3.Id).Current.Name + '] for the Save answer')
    # A packaged instance cannot be started on a file, so the relaunch opens the copy through its own
    # picker again -- the same route the first launch took (sec.118: without this the Save round ran
    # against an empty window and found Rotate Pages grey).
    if ($Installed -and $OpenViaPicker) {
      if (-not (Open-PackagedDocument $app3 $OpenViaPicker)) { Bail '  no document reached the relaunched packaged instance' }
    }
    Rotate-CurrentPage $app3.Id
    if (-not (Wait-Star $app3.Id $true)) { Bail '  the caption never grew the star in the Save round' }
    $hashBeforeSave = Copy-Hash
    if ($hashBeforeSave -ne $hashOpen) {
      Bail ('  the copy changed while nothing was saved (it was [' + $hashOpen + '] and is now [' + $hashBeforeSave + '])')
    }
    if (-not (Close-Window $app3.Id)) { Bail '  no window to close in the Save round' }
    $box3 = Prompt-Three $app3.Id 'save'
    if (-not (PressBoxButton $box3 $answerSave 'save')) { Bail '  the box has no Save button in either language' }
    # The save runs before the window goes: drain the readers, write beside the file, verify, replace.
    if (-not (Wait-WindowGone $app3.Id 40)) {
      PrintCandidates $app3.Id 'the window is still here after the Save answer; every candidate window:'
      Bail '  the Save answer did not close the window, so the save did not finish'
    }
    $hashAfterSave = Copy-Hash
    if ($hashAfterSave -eq $hashBeforeSave) {
      Bail '  the window closed on the Save answer but the copy is byte-for-byte what it was'
    }
    # A moved hash alone is not enough: a rewrite that only refreshed its timestamps would move it.
    $raw = [System.Text.Encoding]::GetEncoding('ISO-8859-1').GetString([System.IO.File]::ReadAllBytes($script:guardCopy))
    $rotated = $raw.Contains('/Rotate 90')
    Say ('  save round: copy [' + $hashBeforeSave + '] -> [' + $hashAfterSave + ']  /Rotate 90 present=' + $rotated)
    if (-not $rotated) {
      Bail '  the copy moved but carries no /Rotate 90 -- what was written is not the rotation that was asked for'
    }
    if (-not (Guard-Check 'after the three answers')) {
      Bail '  one of the three answers wrote the file this probe was handed rather than its own copy'
    }
    Say '  the three answers do what they say: Cancel keeps, Discard closes clean, Save writes and closes'
  }
  'saveas' {
    # Save As: the chord, the native box, a name written into it, and the file that comes out.
    #
    # This mode was written against the row that used to be called 'Save Changes As' (sec.44, and
    # the route notes below are its measurements). F8 renamed that row in two: the plain Save (Ctrl+S)
    # now writes in place, and 'Save As...' (Ctrl+Shift+S) is the old behaviour under its own name
    # (mainwindow.cpp:1032 and :1042). Two things followed. The old needles match nothing at all now
    # -- no row is called 'Save Changes As' in either language -- and the Ctrl+S this mode used to
    # press would write the file it was handed. That is why it sat unrun from sec.116 on, and why it
    # now works entirely on the copy the guard made: the file Save As writes lands in that same probe
    # directory, and BOTH the original and the copy have to be untouched when the run ends.
    #
    # The route through the native box is still neither of the two obvious ones (sec.44 measured it,
    # and it cost sec.41 a whole round of wrong conclusions):
    #   * WM_SETTEXT to the Edit with control id 1001 DOES change the name -- proven by the file that
    #     comes out, because WM_GETTEXT on that same Edit answers empty while the screen shows text,
    #     so the box cannot be read back at all.
    #   * BM_CLICK to the button with control id 1 does NOT accept this dialog, even though the same
    #     call accepts the native open dialog (sec.29) and the folder picker (sec.37/38). What
    #     accepts it is a real click at the button's own GetWindowRect centre.
    #   * The keyboard is not an option INSIDE the box: SendKeys there gets eaten by the Pinyin IME
    #     ('renamed-44.pdf' came out as composition text with a candidate strip open). The chord that
    #     opens it goes to our own window, which is a different matter and is foreground-verified.
    # Acceptance is read from our own receipts: the file on disk, the caption, the status line, and
    # the two hashes that must not move.
    # -Control keeps every one of those expectations and presses nothing: no box, no file, exit 1.
    $target = Join-Path $script:guardDir ('saveas-' + $script:guardCopyLeaf)
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
    $copyBefore = Copy-Hash
    Say ('  the new file will be [' + $target + '] -- inside the probe directory, never beside the original')

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

    # An edit first: Save As with an empty edit list refuses ('there is nothing to save'), so the box
    # would never open and the run would read as a broken route instead of as what it is.
    $win = TakeForeground $app.Id
    if (-not $win) { Bail '  could not take the foreground; the chord would go to another window' }
    if (-not (Window $app.Id).Current.Name.Contains($script:guardCopyLeaf)) {
      Bail ('  the caption does not name the copy this probe made [' + $script:guardCopyLeaf + ']')
    }
    Say ('  window [' + (Window $app.Id).Current.Name + ']')
    Rotate-CurrentPage $app.Id
    if (-not (Wait-Star $app.Id $true)) {
      Bail ('  the caption never grew the unsaved-edits star; it reads [' + (Window $app.Id).Current.Name + ']')
    }
    Say ('  caption after the rotation [' + (Window $app.Id).Current.Name + '] -- the star is there')

    # The chord is the reader's own route and it cannot miss a menu row; the row is the retry, with
    # the retries Expand() has always needed here (it reports the menu as open on rounds where the
    # following click does not land). -Control skips BOTH and keeps every verdict below.
    $sv = $null
    if ($Control) {
      Say '  CONTROL: neither Ctrl+Shift+S nor the Save As row is pressed; every verdict stays'
    } else {
      if (-not (ForegroundIs (Window $app.Id))) {
        Say ('  foreground is [' + [W32]::Text([W32]::GetForegroundWindow()) + '], not ours; raising')
        if (-not (TakeForeground $app.Id)) { Bail '  could not get the foreground back after the rotate dialog' }
      }
      [System.Windows.Forms.SendKeys]::SendWait('^+s')
      for ($k = 1; $k -le 10 -and -not $sv; $k++) {
        Start-Sleep -Milliseconds 500
        $sv = FindPicker $app.Id
      }
      if ($sv) { Say ('  Ctrl+Shift+S opened the native box [' + $sv.Current.Name + ']') }
      for ($try = 1; $try -le 3 -and -not $sv; $try++) {
        if ($try -eq 1) { Say '  no box within 5s of the chord; trying the Document menu row' }
        TopMenu $app.Id @('Document', (CjStr 0x6587, 0x6863)) | Out-Null
        $saveRow = RowAny $app.Id @('Save As', (CjStr 0x53E6, 0x4FDD, 0x4E3A))
        if (-not $saveRow) { Bail '  no Save As row under Document in either language' }
        $rr = $saveRow.Current.BoundingRectangle
        Say ('  try ' + $try + ': menu row |' + $saveRow.Current.Name + '| enabled=' + $saveRow.Current.IsEnabled +
             ' at ' + [int]$rr.X + ',' + [int]$rr.Y + ' ' + [int]$rr.Width + 'x' + [int]$rr.Height)
        ClickAt $saveRow
        for ($k = 1; $k -le 8 -and -not $sv; $k++) {
          Start-Sleep -Milliseconds 500
          $sv = FindPicker $app.Id
        }
      }
    }
    if (-not $sv) {
      if (-not $Control) { Bail '  no native save dialog after the chord and three menu tries' }
    } else {
      Say ('  save dialog [' + $sv.Current.Name + '] class=' + $sv.Current.ClassName)
      $kids = PickerChildren $sv
      $go = $null
      foreach ($h in $kids) {
        $cls = [W32]::Cls($h); $id = [W32]::GetDlgCtrlID($h)
        # The text printed here is what Win32 ANSWERS, not what is drawn: on the name field it is
        # empty while the screen shows the seeded file name (sec.44).
        if ($cls -eq 'Edit') { Say ('    Edit id=' + $id + ' answers [' + [W32]::Text($h) + ']') }
        if ($cls -eq 'Button' -and $id -eq 1) { $go = $h }
      }
      $nameBox = NameField $kids
      if (-not ($nameBox -and $go)) { Bail '  no file-name field / no Button id=1 in the save box' }
      [W32]::SendMessageW($nameBox, 0x000C, [IntPtr]0, $target) | Out-Null
      Start-Sleep -Milliseconds 700
      Say ('  wrote [' + $target + '], box answers [' + [W32]::Text($nameBox) +
           '] -- an empty answer proves nothing, see the note above')
      # A real click on the drawn button: BM_CLICK to this control id is answered by the open dialog
      # and the folder picker, but NOT by the save dialog (sec.44).
      $gr = [W32]::Rect([IntPtr]$go)
      [W32]::Click([int](($gr.left + $gr.right) / 2), [int](($gr.top + $gr.bottom) / 2))
    }

    # ---- the verdicts, the same in both runs ----
    $exists = $false
    for ($k = 1; $k -le 24 -and -not $exists; $k++) {
      $exists = Test-Path -LiteralPath $target
      if (-not $exists) { Start-Sleep -Milliseconds 500 }
    }
    $size = 0
    if ($exists) { $size = (Get-Item -LiteralPath $target).Length }
    Say ('  target exists=' + $exists + '  size=' + $size)
    if (-not $exists) {
      if ($Control) {
        Bail '  CONTROL: with nothing pressed no file came out -- the target verdict is live (CONTROL exit 1)'
      }
      Bail ('  the save box was accepted but no file came out at [' + $target + ']')
    }
    # The status line is timed at 8 s, so it is read before anything else is polled.
    $wroteCj = CjStr 0x5DF2, 0x5199, 0x6210   # the source form of the 'wrote a new file' line
    $msg = Wait-Status $app.Id @($wroteCj, 'Wrote a new file')
    # Save As makes THIS tab the new file -- the documentSavedAs handler stopped opening a second tab
    # at F8 -- so the caption is the receipt that the app itself took the box's answer.
    $targetLeaf = [System.IO.Path]::GetFileName($target)
    $renamed = $false
    for ($k = 1; $k -le 20 -and -not $renamed; $k++) {
      $renamed = $null -ne (Window $app.Id) -and (Window $app.Id).Current.Name.Contains($targetLeaf)
      if (-not $renamed) { Start-Sleep -Milliseconds 300 }
    }
    Say ('  status line [' + $msg.Replace("`r", ' ').Replace("`n", ' / ') + ']')
    Say ('  caption [' + (Window $app.Id).Current.Name + '] names the new file=' + $renamed)
    foreach ($t in @(FindType (Window $app.Id) $CT::TabItem)) { Say ('  tab |' + $t.Current.Name + '|') }
    if ($msg -eq '') { Bail '  no status line about a new file being written' }
    if (-not $renamed) { Bail ('  the caption never became the new file [' + $targetLeaf + ']') }
    # The two files that must not have moved: the copy the app was opened with, and the original.
    $copyAfter = Copy-Hash
    Say ('  in-place copy sha256 [' + $copyBefore + '] -> [' + $copyAfter + ']')
    if ($copyAfter -ne $copyBefore) {
      Bail '  Save As wrote the in-place file as well -- that is the Ctrl+S route, not this one'
    }
    # A file that exists is not yet the edited document: a copy of the untouched source would exist too.
    $raw = [System.Text.Encoding]::GetEncoding('ISO-8859-1').GetString([System.IO.File]::ReadAllBytes($target))
    $rotated = $raw.Contains('/Rotate 90')
    Say ('  the new file carries /Rotate 90=' + $rotated)
    if (-not $rotated) { Bail '  the new file is a rewrite of the untouched source -- the rotation is not in it' }
    if (-not (Guard-Check 'after Save As')) { Bail '  Save As wrote the original file this probe was handed' }
    Say '  Save As wrote its own new file, left the copy and the original alone, and the tab moved to it'
  }
  'wordexport' { Export-Round 'word' }
  'excelexport' { Export-Round 'excel' }
  'pptexport' { Export-Round 'ppt' }
  'searchablepdf' { Export-Round 'searchable' }
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

    # -Keep keeps the LANGUAGE, not the instance: a bare exit here skipped the kill at the end of the
    # file and left the packaged copy running into the next round (sec.130, after four -Keep runs).
    if ($Keep) { Say '  -Keep set: the language is left as switched'; Kill-ProbeApp; exit 0 }

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
  'formfill' {
    # Type into a field with a real click and real keys, then prove it landed three ways: the ink
    # inside the field's own rectangle grew while a blank rectangle beside it did not move, the
    # caption's star came and went around Ctrl+S, and the copy on disk answers with exactly the
    # token that was typed -- in the aimed field and in none of its neighbours. -Control clicks the
    # same field and types nothing: no star, no ink, no save line, and it still expects all of that,
    # so it exits 1.
    Form-TitleCheck 'formfill'
    Read-FormFields $script:guardCopy 'formfill-before' | Out-Null
    $tgt = Field-At 'alpha' 'formfill'
    if ($tgt.value -ne '') {
      Bail ('  formfill: alpha is not empty before anything was typed: [' + (UForm $tgt.value) + ']')
    }
    $shot = Zoom-WholePage 'formfill'
    foreach ($f in @($script:ffFields)) {
      $m = Map-Rect $shot $f.l $f.b $f.r $f.t
      Say ('  field ' + $f.name + ' ' + $f.type + ' user ' + $f.l + ',' + $f.b + '-' + $f.r + ',' + $f.t +
           ' -> bitmap ' + [int]$m.bx0 + ',' + [int]$m.by0 + '-' + [int]$m.bx1 + ',' + [int]$m.by1 +
           ' screen ' + [int]$m.sx + ',' + [int]$m.sy)
    }
    Click-Field $shot $tgt 'formfill'
    # A focused text field paints its own caret, and the caret BLINKS -- so "the ink inside the
    # field before anything was typed" is not one number. Measured in the same rectangle one blink
    # apart with the keys never touched: 0, then 24. The baseline is therefore the larger of two
    # shots taken a blink apart, and the reading the verdict is written on is a THIRD shot, taken
    # after a click on blank paper has made the field let go of the keyboard: no caret is left in
    # it to be mistaken for a glyph, in either direction.
    $shotA1 = Page-Shot 'formfill-focused-1'
    $inkA1 = Region-Ink $shotA1 (Map-Rect $shotA1 $tgt.l $tgt.b $tgt.r $tgt.t)
    Start-Sleep -Milliseconds 700
    $shotA2 = Page-Shot 'formfill-focused-2'
    $inkA2 = Region-Ink $shotA2 (Map-Rect $shotA2 $tgt.l $tgt.b $tgt.r $tgt.t)
    $inkBefore = [math]::Max($inkA1, $inkA2)
    $blankBefore = Region-Ink $shotA2 (Map-Rect $shotA2 300 700 500 730)
    Say ('  ink in alpha while it holds the keyboard with nothing typed: ' + $inkA1 + ' then ' + $inkA2 +
         ' -- a caret, and it blinks, so the baseline is the larger: ' + $inkBefore)
    if ($Control) {
      Say '  CONTROL: nothing is typed; the star, the ink and the save line are still expected to stay put'
      $token = ''
    } else {
      $token = 'P' + [guid]::NewGuid().ToString('N').Substring(0, 6).ToUpper()
      Say ('  formfill: typing [' + $token + '] into alpha')
      Type-Text $token 'formfill'
    }
    Start-Sleep -Milliseconds 700
    if ($Control) {
      if (Wait-Star $app.Id $true 8) {
        Bail '  CONTROL: the caption grew the unsaved-edits star with nothing typed -- a focus change is being counted as an edit'
      }
      Say '  CONTROL: no star with nothing typed, as it should be'
    } elseif (-not (Wait-Star $app.Id $true)) {
      Bail ('  formfill: the caption never grew the unsaved-edits star after typing; it reads [' +
            (Window $app.Id).Current.Name + ']')
    }
    $shotB = Page-Shot 'formfill-after-typing'
    $inkAfter = Region-Ink $shotB (Map-Rect $shotB $tgt.l $tgt.b $tgt.r $tgt.t)
    $blankMid = Region-Ink $shotB (Map-Rect $shotB 300 700 500 730)
    Say ('  ink in alpha with the keyboard still held: before=' + $inkBefore + ' after=' + $inkAfter +
         '; blank rectangle beside it before=' + $blankBefore + ' after=' + $blankMid)
    if ($blankBefore -ne $blankMid) {
      Bail ('  formfill: the blank rectangle beside the field changed ink (' + $blankBefore + ' -> ' +
            $blankMid + '), so the picture moved between the two shots and no ink number compares')
    }
    Click-Blank 'formfill'
    $shotC = Page-Shot 'formfill-committed'
    $inkCommitted = Region-Ink $shotC (Map-Rect $shotC $tgt.l $tgt.b $tgt.r $tgt.t)
    $blankAfter = Region-Ink $shotC (Map-Rect $shotC 300 700 500 730)
    Say ('  ink in alpha once it has let the keyboard go: ' + $inkCommitted +
         ' against the baseline ' + $inkBefore + '; blank rectangle ' + $blankAfter)
    if ($blankAfter -ne $blankBefore) {
      Bail ('  formfill: the blank rectangle beside the field changed ink (' + $blankBefore + ' -> ' +
            $blankAfter + ') across the click on paper, so the page moved and no ink number compares')
    }
    # Tripling, plus a floor: a caret caught in its ON phase is a couple of dozen pixels and seven
    # glyphs are hundreds, so the bar has to be one a caret cannot clear even when the baseline
    # itself was measured with the caret off.
    $need = ($inkBefore * 3) + 50
    if ($Control) {
      if ($inkCommitted -gt $inkBefore) {
        Bail ('  CONTROL: ink stayed inside the field after the keyboard was let go (' + $inkBefore +
              ' -> ' + $inkCommitted + ') with nothing typed into it')
      }
    } elseif ($inkCommitted -lt $need) {
      Bail ('  formfill: typing [' + $token + '] left ' + $inkCommitted + ' px of ink in the field it ' +
            'was aimed at, and a bare focused field already paints ' + $inkBefore + ' -- the glyphs ' +
            'are not on screen, so the screen half of this mode proves nothing')
    }
    $sv = Save-AndRead 'formfill' (-not $Control)
    Read-FormFields $script:guardCopy 'formfill-after' | Out-Null
    $a = Field-At 'alpha' 'formfill'; $b = Field-At 'beta' 'formfill'
    $g = Field-At 'gamma' 'formfill'; $d = Field-At 'delta' 'formfill'; $c = Field-At 'agree' 'formfill'
    Say ('  read back: alpha=[' + (UForm $a.value) + '] beta=[' + (UForm $b.value) + '] gamma=[' +
         (UForm $g.value) + '] delta=[' + (UForm $d.value) + '] agree=[' + (UForm $c.value) + ']')
    if ($Control) {
      if ($sv.sawMsg) { Bail '  CONTROL: a save line appeared with Ctrl+S unpressed -- the status verdict is not live' }
      if ($sv.moved) { Bail '  CONTROL: the copy moved although nothing was saved' }
      if ($a.value -ne '') { Bail ('  CONTROL: alpha came back [' + (UForm $a.value) + '] with nothing typed into it') }
      Bail '  CONTROL: formfill with the keys unpressed produced no star, no ink, no save line and no moved bytes (CONTROL exit 1)'
    }
    Check-Save $sv 'formfill'
    if ((UForm $a.value) -ne $token) {
      Bail ('  formfill: alpha came back [' + (UForm $a.value) + '], not the typed [' + $token + ']')
    }
    foreach ($f in @($b, $g, $d)) {
      if ($f.value -ne '') {
        Bail ('  formfill: ' + $f.name + ' came back [' + (UForm $f.value) + '] though nothing was typed into it')
      }
    }
    if ((UForm $c.value) -ne 'Off') { Bail ('  formfill: agree came back [' + (UForm $c.value) + '], not Off') }
    if (-not (Guard-Check 'after formfill')) { Bail '  formfill: the original moved during the run' }
    Say ('  formfill: [' + $token + '] was typed into alpha on screen, saved, and the copy answers with it')
  }
  'formtab' {
    # Tab order: click the first field, press Tab, type -- the letters have to land in the SECOND
    # field, because that is what Tab is for. Shift+Tab carries the keyboard back and the second
    # token lands in the first. The CARET is read while both fields are empty (click on alpha, after
    # Tab) because it is the only on-screen mark of which field holds the keyboard; once a token has
    # been typed the receipt is the read-back, since Caret-Map counts ink and glyphs look like a
    # caret. The read-back has to answer beta with token one and alpha with token two. -Control skips
    # both Tab presses, so the caret never moves and both tokens pile up in alpha while beta stays
    # empty -- the same expectation, therefore exit 1.
    Form-TitleCheck 'formtab'
    Read-FormFields $script:guardCopy 'formtab-before' | Out-Null
    $alpha = Field-At 'alpha' 'formtab'; $beta = Field-At 'beta' 'formtab'
    if ($alpha.value -ne '' -or $beta.value -ne '') {
      Bail ('  formtab: alpha or beta is not empty before anything was typed: [' + (UForm $alpha.value) +
            '] / [' + (UForm $beta.value) + ']')
    }
    $shot = Zoom-WholePage 'formtab'
    Click-Field $shot $alpha 'formtab'
    $p0 = Caret-Map 'formtab-caret-on-click'
    Caret-Expect $p0 @('alpha') 'formtab after the click on alpha'
    # ---- the key channel, proven before it is used ----
    #
    # Everything after this is one Tab press sent as a virtual-key code, and if that channel does not
    # reach the page then "the caret did not move" is a statement about SendInput, not about Tab. Two
    # things were measured on the way here and they are not the same thing: KEYEVENTF_UNICODE
    # characters DO reach the field (the caret's 24 px goes to 92 for two glyphs), while two
    # Backspaces by virtual-key code left the field reading exactly 92 -- so the character route
    # working says nothing about the virtual-key route, and Tab uses the virtual-key one.
    #
    # Esc is the one virtual key whose arrival is unambiguous here, because PageView answers it
    # itself before PDFium is asked (releaseFocus at pageview.cpp's keyPressEvent, then the caret is
    # painted away). It also leaves nothing behind, which is why it is the control rather than a
    # Backspace: a key that turns out not to arrive must not leave text in the field the rest of this
    # mode is about to read back. 24 px -> 0 is the receipt; 24 px -> 24 px is the harness.
    Press-Key 0x1B $false 'formtab-keycontrol'
    Start-Sleep -Milliseconds 500
    $inkEscaped = Field-Ink 'formtab-keycontrol-escaped' $alpha
    Say ('  key control: the caret was ' + $p0['alpha'] + ' px, and Esc by virtual-key code left ' +
         $inkEscaped + ' px in alpha')
    if ($inkEscaped -gt 10) {
      Bail ('  formtab: Esc by virtual-key code left the field exactly as it was (' + $p0['alpha'] + ' -> ' +
            $inkEscaped + ' px) -- the caret only goes out when releaseFocus runs, so the virtual key ' +
            'never reached the page and a Tab press after this would prove nothing about Tab')
    }
    Say '  key control: the virtual key reached the page and the field let go of the keyboard'
    Click-Field $shot $alpha 'formtab'
    $p0b = Caret-Map 'formtab-caret-again'
    Caret-Expect $p0b @('alpha') 'formtab after clicking alpha again'
    $starBefore = Has-Star $app.Id
    Say ('  formtab: caption after the key control [' + (Window $app.Id).Current.Name + '], star=' + $starBefore)
    if ($Control) {
      Say '  CONTROL: Tab and Shift+Tab are not pressed; both tokens are still expected where Tab would have put them'
    } else {
      Say ('  formtab: keyboard focus before Tab is ' + (Focus-Now))
      Say '  formtab: pressing Tab; the keyboard should move to beta'
      Press-Key 0x09 $false 'formtab'
      Say ('  formtab: keyboard focus after Tab is ' + (Focus-Now))
    }
    $p1 = Caret-Map 'formtab-caret-after-tab'
    if ($Control) {
      Caret-Expect $p1 @('alpha') 'CONTROL with Tab unpressed'
      Say '  CONTROL: the caret stayed in alpha, as it must with Tab unpressed'
    } else {
      # This is the verdict that matters, and it is written to say which of the two failures it saw:
      # "the caret is nowhere" (the keyboard was dropped) is a different product defect from "the
      # caret is in gamma" (Tab walked the fields in an order this sample does not set).
      Caret-Expect $p1 @('beta') 'formtab after Tab'
      Say '  formtab: the caret is in beta and gone from alpha -- Tab moved the keyboard'
    }
    $t1 = 'T1' + [guid]::NewGuid().ToString('N').Substring(0, 4).ToUpper()
    Type-Text $t1 'formtab-1'
    if (-not $Control) {
      Say '  formtab: pressing Shift+Tab; the keyboard should move back to alpha'
      Press-Key 0x09 $true 'formtab'
    }
    # No caret reading here. Caret-Map counts ink, and beta now holds $t1 -- glyphs sitting still look
    # exactly like a caret to a count, so the caret cannot say which field has the keyboard once either
    # one has text in it. The receipt for "the keyboard came back" is the second token itself: $t1 and
    # $t2 differ by construction, so the file says which field got which, and a mismatch says whether
    # Shift+Tab did nothing (both tokens in beta) or went somewhere else (alpha still empty).
    $t2 = 'T2' + [guid]::NewGuid().ToString('N').Substring(0, 4).ToUpper()
    Type-Text $t2 'formtab-2'
    Start-Sleep -Milliseconds 700
    if ($starBefore) {
      # The key control only pressed Esc, but if a focus change already counted as an edit the caption
      # carries the star before either token exists. A verdict that cannot fail is decoration, so it
      # is printed instead of asserted, and said out loud.
      Say ('  formtab: the caption already carried the star before the tokens were typed, so the star ' +
           'is not evidence in this run; the read-back is')
    } elseif (-not (Wait-Star $app.Id $true)) {
      Bail ('  formtab: the caption never grew the star after typing; it reads [' + (Window $app.Id).Current.Name + ']')
    } else {
      Say '  formtab: the caption grew the unsaved-edits star'
    }
    Click-Blank 'formtab'
    $sv = Save-AndRead 'formtab' $true
    Check-Save $sv 'formtab'
    Read-FormFields $script:guardCopy 'formtab-after' | Out-Null
    $a = Field-At 'alpha' 'formtab'; $b = Field-At 'beta' 'formtab'
    $g = Field-At 'gamma' 'formtab'; $d = Field-At 'delta' 'formtab'
    Say ('  read back: alpha=[' + (UForm $a.value) + '] beta=[' + (UForm $b.value) + '] gamma=[' +
         (UForm $g.value) + '] delta=[' + (UForm $d.value) + ']')
    if ((UForm $b.value) -ne $t1) {
      if ($Control) {
        Bail ('  CONTROL: beta came back [' + (UForm $b.value) + '], not [' + $t1 + '] -- with Tab unpressed the ' +
              'letters stayed where they were typed (CONTROL exit 1)')
      }
      Bail ('  formtab: beta came back [' + (UForm $b.value) + '], not [' + $t1 + '] -- the letters did not follow Tab')
    }
    if ((UForm $a.value) -ne $t2) {
      Bail ('  formtab: alpha came back [' + (UForm $a.value) + '], not [' + $t2 + '] -- Shift+Tab did not carry the ' +
            'keyboard back; beta holds [' + (UForm $b.value) + ']')
    }
    foreach ($f in @($g, $d)) {
      if ($f.value -ne '') {
        Bail ('  formtab: ' + $f.name + ' came back [' + (UForm $f.value) + '] though nothing was typed into it')
      }
    }
    if (-not (Guard-Check 'after formtab')) { Bail '  formtab: the original moved during the run' }
    Say ('  formtab: Tab put [' + $t1 + '] in beta and Shift+Tab put [' + $t2 + '] back in alpha')
  }
  'formcheck' {
    # The checkbox: one click paints its /Yes appearance (a filled square in the sample, which is the
    # pixel difference this mode reads), the caption grows the star, the save writes /V /Yes, and a
    # second click takes all of it back to /Off. -Control clicks nothing and still expects /Yes, so
    # it exits 1.
    Form-TitleCheck 'formcheck'
    Read-FormFields $script:guardCopy 'formcheck-before' | Out-Null
    $box = Field-At 'agree' 'formcheck'
    if ((UForm $box.value) -ne 'Off') {
      Bail ('  formcheck: agree starts at [' + (UForm $box.value) + '], not Off')
    }
    $shot = Zoom-WholePage 'formcheck'
    $mA = Map-Rect $shot $box.l $box.b $box.r $box.t
    Assert-InCrop $shot $mA 'formcheck agree'
    $inkBefore = Region-Ink $shot $mA
    if ($Control) {
      Say '  CONTROL: the checkbox is not clicked; /V is still expected to become Yes'
    } else {
      Click-Field $shot $box 'formcheck'
      if (-not (Wait-Star $app.Id $true)) {
        Bail ('  formcheck: the caption never grew the star after the click; it reads [' + (Window $app.Id).Current.Name + ']')
      }
    }
    $shotB = Page-Shot 'formcheck-after-click'
    $mB = Map-Rect $shotB $box.l $box.b $box.r $box.t
    $inkAfter = Region-Ink $shotB $mB
    Say ('  ink in the checkbox box: before=' + $inkBefore + ' after=' + $inkAfter)
    if ($Control) {
      if ($inkAfter -ne $inkBefore) {
        Bail ('  CONTROL: ink moved inside the checkbox with no click (' + $inkBefore + ' -> ' + $inkAfter + ')')
      }
    } elseif ($inkAfter -le $inkBefore) {
      Bail ('  formcheck: the click painted nothing in the checkbox''s own box (' + $inkBefore + ' -> ' + $inkAfter + ')')
    }
    Click-Blank 'formcheck'
    $sv = Save-AndRead 'formcheck' (-not $Control)
    Read-FormFields $script:guardCopy 'formcheck-after' | Out-Null
    $c = Field-At 'agree' 'formcheck'
    Say ('  read back: agree=[' + (UForm $c.value) + ']')
    if ($Control) {
      if ($sv.sawMsg) { Bail '  CONTROL: a save line appeared with Ctrl+S unpressed -- the status verdict is not live' }
      if ((UForm $c.value) -ne 'Off') { Bail ('  CONTROL: agree came back [' + (UForm $c.value) + '] with no click') }
      Bail '  CONTROL: formcheck with the click unpressed produced no star, no ink and no /Yes (CONTROL exit 1)'
    }
    Check-Save $sv 'formcheck'
    if ((UForm $c.value) -ne 'Yes') {
      Bail ('  formcheck: agree came back [' + (UForm $c.value) + '], not Yes')
    }
    # Second half: the same click has to undo itself. The save reopened the document, so the zoom is
    # fit-width again and the page has to be found in the picture once more.
    $shotC = Zoom-WholePage 'formcheck-2'
    $mC = Map-Rect $shotC $box.l $box.b $box.r $box.t
    Assert-InCrop $shotC $mC 'formcheck agree, second half'
    $inkChecked = Region-Ink $shotC $mC
    Click-Field $shotC $box 'formcheck-2'
    if (-not (Wait-Star $app.Id $true)) {
      Bail ('  formcheck: the caption never grew the star after the second click; it reads [' + (Window $app.Id).Current.Name + ']')
    }
    $shotD = Page-Shot 'formcheck-after-second-click'
    $mD = Map-Rect $shotD $box.l $box.b $box.r $box.t
    $inkBack = Region-Ink $shotD $mD
    Say ('  ink checked=' + $inkChecked + ' after the second click=' + $inkBack)
    if ($inkBack -ge $inkChecked) {
      Bail ('  formcheck: the second click did not take the fill away (' + $inkChecked + ' -> ' + $inkBack + ')')
    }
    Click-Blank 'formcheck-2'
    $sv2 = Save-AndRead 'formcheck-2' $true
    Check-Save $sv2 'formcheck-2'
    Read-FormFields $script:guardCopy 'formcheck-after-2' | Out-Null
    $c2 = Field-At 'agree' 'formcheck-2'
    Say ('  read back: agree=[' + (UForm $c2.value) + ']')
    if ((UForm $c2.value) -ne 'Off') {
      Bail ('  formcheck: the second click left agree at [' + (UForm $c2.value) + '], not Off')
    }
    if (-not (Guard-Check 'after formcheck')) { Bail '  formcheck: the original moved during the run' }
    Say '  formcheck: the click painted the box and wrote /Yes, the second click took both back'
  }
  'formime' {
    # The one route that asks an input method to hand over a composed string, and the two comparisons
    # H2 asks to see beside it. Each route writes a DIFFERENT string into a DIFFERENT field, so the
    # saved file says which route's text reached which field and this file never has to infer "who owns
    # the keyboard" from a caret once text exists (Caret-Map counts ink, so typed glyphs look exactly
    # like a caret). Measured reason for that rule: an earlier round typed route B's characters after
    # clicking delta, they landed in gamma, and the mode's own message blamed the composition -- the
    # click was fine and the composition had never even been attempted; the second click had not moved
    # the form's focus, and a log that checks only one field cannot tell those two apart.
    #
    #   A  gamma -- ImmGetContext on the app's window, ImmSetCompositionString(GCS_COMPSTR) with the
    #              two characters, ImmNotifyIME(NI_COMPOSITIONSTR, CPS_COMPLETE). This is the route H2
    #              is about. Every call's own answer and last error is printed.
    #   B  delta -- a different pair of characters as KEYEVENTF_UNICODE, one WM_CHAR at a time. A
    #              comparison, never a substitute: that is the per-character path, not FORM_ReplaceSelection.
    #   C  alpha -- real virtual-key presses ('zhongwen' then space), so whatever input method the app
    #              thread has gets the keys a keyboard would send. Also a comparison.
    #
    # If A cannot be driven (no context in the first place), every step is printed and the mode exits
    # 1 with B's and C's readings beside it -- exactly the answer H2 allows, and no pretending.
    Form-TitleCheck 'formime'
    Read-FormFields $script:guardCopy 'formime-before' | Out-Null
    $gamma = Field-At 'gamma' 'formime'; $delta = Field-At 'delta' 'formime'; $alpha = Field-At 'alpha' 'formime'
    if ($gamma.value -ne '' -or $delta.value -ne '' -or $alpha.value -ne '') {
      Bail '  formime: gamma, delta or alpha is not empty before anything was typed'
    }
    $wantCj = CjStr 0x4E2D, 0x6587
    $cmpCj = CjStr 0x6C49, 0x5B57
    $wantU = UForm $wantCj
    $cmpU = UForm $cmpCj
    Say ('  formime: route A commits [' + $wantU + ']; route B types [' + $cmpU + '] into another field, so the two cannot be confused')
    $shot = Zoom-WholePage 'formime'
    # ---- what the platform offers before anything is composed ----
    $hwnd = [IntPtr](Window $app.Id).Current.NativeWindowHandle
    $tid = [W32]::GetWindowThreadProcessId($hwnd, [IntPtr]::Zero)
    Say ('  IMM32: main hwnd ' + $hwnd + '; its thread ' + $tid + ' is in layout ' + (Layout-Label ([W32]::GetKeyboardLayout($tid))))
    Say ('  IMM32: this shell thread is in layout ' + (Layout-Label ([W32]::GetKeyboardLayout(0))))
    $hkls = @([W32]::KeyboardLayouts())
    if ($hkls.Count -eq 0) { Say '  IMM32: GetKeyboardLayoutList returned no layouts' }
    foreach ($hkl in $hkls) { Say ('  IMM32: installed layout ' + (Layout-Label $hkl)) }
    try {
      foreach ($lang in @(Get-WinUserLanguageList)) {
        foreach ($tip in @($lang.InputMethodTips)) { Say ('  input method: ' + $lang.LanguageTag + ' ' + $tip) }
      }
    } catch {
      Say ('  input method: the list could not be read (' + $_.Exception.Message + ')')
    }
    # The channel's own control: a window in THIS shell, created and never shown. If the app's window
    # answers zero while this one answers a handle, the zero is about the app's window; if this one also
    # answers zero, nothing this session can reach has a context and the app's zero proves nothing.
    $ctlForm = New-Object System.Windows.Forms.Form
    $ctlForm.CreateControl()
    $ctlH = $ctlForm.Handle
    $ctlHimc = [W32]::ImmGetContext($ctlH)
    Say ('  IMM32 control: a never-shown window in this shell (hwnd ' + $ctlH + ') -> ImmGetContext ' + $ctlHimc)
    if ($ctlHimc -ne [IntPtr]::Zero) { [W32]::ImmReleaseContext($ctlH, $ctlHimc) | Out-Null }
    $ctlAssoc = [W32]::ImmAssociateContextEx($ctlH, [IntPtr]::Zero, [W32]::IACE_DEFAULT)
    $ctlErr = [W32]::LastError()
    $ctlHimc2 = [W32]::ImmGetContext($ctlH)
    Say ('  IMM32 control: ImmAssociateContextEx(IACE_DEFAULT) -> ' + $ctlAssoc + ' (last error ' + $ctlErr +
         '), ImmGetContext then -> ' + $ctlHimc2)
    if ($ctlHimc2 -ne [IntPtr]::Zero) { [W32]::ImmReleaseContext($ctlH, $ctlHimc2) | Out-Null }
    $ctlForm.Dispose()
    # ---- route A: a real composition, committed through IMM32 ----
    Click-Field $shot $gamma 'formime-A'
    $ca = Caret-Map 'formime-A-caret'
    Caret-Expect $ca @('gamma') 'formime route A before the composition (the click on gamma)'
    $aHimc = [IntPtr]::Zero
    $aDriven = $false
    if ($Control) {
      Say '  route A: -Control makes no context request on the app window and no composition; gamma stays empty on purpose'
    } else {
      $aHimc = [W32]::ImmGetContext($hwnd)
      Say ('  route A: ImmGetContext(main hwnd) -> ' + $aHimc + ' (zero = no input context associated with that window)')
      Say ('  route A: ImmGetDefaultIMEWnd(main hwnd) -> ' + [W32]::ImmGetDefaultIMEWnd($hwnd))
      if ($aHimc -eq [IntPtr]::Zero) {
        # The thread's layout is the one thing a caller outside the window can ask it to change, and
        # an input method answers only for a layout that is active there.
        $zh = [IntPtr]::Zero
        foreach ($hkl in $hkls) {
          if (([uint32]($hkl.ToInt64() -band [int64]0xFFFF)) -eq 0x0804) { $zh = $hkl }
        }
        if ($zh -eq [IntPtr]::Zero) {
          Say '  route A: no zh-CN layout is installed, so there is none to ask the thread for'
        } else {
          $posted = [W32]::PostMessageW($hwnd, [W32]::WM_INPUTLANGCHANGEREQUEST, [IntPtr]::Zero, $zh)
          Start-Sleep -Milliseconds 700
          $tid2 = [W32]::GetWindowThreadProcessId($hwnd, [IntPtr]::Zero)
          Say ('  route A: WM_INPUTLANGCHANGEREQUEST to ' + (Layout-Label $zh) + ' posted=' + $posted +
               '; the thread is now in ' + (Layout-Label ([W32]::GetKeyboardLayout($tid2))))
          $aHimc = [W32]::ImmGetContext($hwnd)
          Say ('  route A: ImmGetContext after the layout request -> ' + $aHimc)
        }
      }
      if ($aHimc -eq [IntPtr]::Zero) {
        $assoc = [W32]::ImmAssociateContextEx($hwnd, [IntPtr]::Zero, [W32]::IACE_DEFAULT)
        $errAssoc = [W32]::LastError()
        $aHimc = [W32]::ImmGetContext($hwnd)
        Say ('  route A: ImmAssociateContextEx(IACE_DEFAULT) -> ' + $assoc + ' (last error ' + $errAssoc +
             '), ImmGetContext then -> ' + $aHimc)
      }
      if ($aHimc -eq [IntPtr]::Zero) {
        Say '  route A: no context was obtained, so no composition is sent -- there is nothing to send it to'
      } else {
        $aOpen = [W32]::ImmGetOpenStatus($aHimc)
        Say ('  route A: ImmGetOpenStatus -> ' + $aOpen)
        if (-not $aOpen) {
          $opened = [W32]::ImmSetOpenStatus($aHimc, $true)
          Say ('  route A: ImmSetOpenStatus(true) -> ' + $opened + ' (last error ' + ([W32]::LastError()) + ')')
        }
        $set = [W32]::ImmSetCompositionString($aHimc, [W32]::GCS_COMPSTR, $wantCj, [uint32]($wantCj.Length * 2), [IntPtr]::Zero, 0)
        $errSet = [W32]::LastError()
        Say ('  route A: ImmSetCompositionString(GCS_COMPSTR, [' + $wantU + ']) -> ' + $set + ' (last error ' + $errSet + ')')
        $compBuf = New-Object System.Text.StringBuilder 64
        $compLen = [W32]::ImmGetCompositionString($aHimc, [W32]::GCS_COMPSTR, $compBuf, [uint32](64 * 2))
        Say ('  route A: ImmGetCompositionString(GCS_COMPSTR) -> ' + $compLen + ' bytes [' + (UForm $compBuf.ToString()) + ']')
        $notify = [W32]::ImmNotifyIME($aHimc, [W32]::NI_COMPOSITIONSTR, [W32]::CPS_COMPLETE, [IntPtr]::Zero)
        $errNotify = [W32]::LastError()
        Say ('  route A: ImmNotifyIME(NI_COMPOSITIONSTR, CPS_COMPLETE) -> ' + $notify + ' (last error ' + $errNotify + ')')
        $resBuf = New-Object System.Text.StringBuilder 64
        $resLen = [W32]::ImmGetCompositionString($aHimc, [W32]::GCS_RESULTSTR, $resBuf, [uint32](64 * 2))
        Say ('  route A: ImmGetCompositionString(GCS_RESULTSTR) -> ' + $resLen + ' bytes [' + (UForm $resBuf.ToString()) + ']')
        [W32]::ImmReleaseContext($hwnd, $aHimc) | Out-Null
        $aDriven = $true
        Start-Sleep -Milliseconds 900
      }
    }
    # ---- route B: KEYEVENTF_UNICODE, into delta, with its own string ----
    #
    # One field can take the keyboard; a click on a DIFFERENT field does not move it. Measured twice
    # in this mode (with and without a mouse-move before the press): after the click on gamma, the
    # click on delta left gamma focused -- the caret map then showed no caret anywhere, and B's two
    # characters came back in gamma with delta empty. So each route lets go first. Esc is the
    # product's own "let go" (pageview.cpp answers it with releaseFocus before PDFium sees a key),
    # and with no field focused the next click takes the field it aims at, which is the state every
    # first click was measured in. The click itself is formtab's subject; this mode is about the
    # three input channels.
    Press-Key 0x1B $false 'formime-letgo-A'
    Start-Sleep -Milliseconds 400
    Click-Field $shot $delta 'formime-B'
    # Instrument, not a verdict: every field is still empty here in a run where route A did not land,
    # so the caret map is valid and it answers "did the click on delta move the keyboard at all".
    # Printed rather than asserted because in a run where A DID land, gamma holds its text and the
    # map cannot tell that text from a caret (see Caret-Map's limit).
    $cb = Caret-Map 'formime-B-caret'
    Say ('  formime-B: after the click on delta the caret is in ' + ((Caret-Holders $cb) -join ', ') +
         ' -- the aim was delta')
    Type-Text $cmpCj 'formime-B'
    # ---- route C: real virtual keys into alpha, then Esc to close any candidate window ----
    Press-Key 0x1B $false 'formime-letgo-B'
    Start-Sleep -Milliseconds 400
    Click-Field $shot $alpha 'formime-C'
    $tidC = [W32]::GetWindowThreadProcessId($hwnd, [IntPtr]::Zero)
    Say ('  route C: the app thread is in ' + (Layout-Label ([W32]::GetKeyboardLayout($tidC))) +
         '; sending z h o n g w e n as virtual keys, then space')
    foreach ($ch in 'zhongwen'.ToCharArray()) {
      Press-Key ([int][char](([string]$ch).ToUpper())) $false 'formime-C'
    }
    Press-Key 0x20 $false 'formime-C'
    Start-Sleep -Milliseconds 900
    Press-Key 0x1B $false 'formime-C-escape'
    Start-Sleep -Milliseconds 400
    Click-Blank 'formime'
    $sv = Save-AndRead 'formime' $true
    Check-Save $sv 'formime'
    Read-FormFields $script:guardCopy 'formime-after' | Out-Null
    $g = Field-At 'gamma' 'formime'; $d = Field-At 'delta' 'formime'; $a = Field-At 'alpha' 'formime'
    $gv = UForm $g.value; $dv = UForm $d.value; $av = UForm $a.value
    Say ('  route readings: A(gamma)=[' + $gv + '] B(delta)=[' + $dv + '] C(alpha)=[' + $av + ']')
    if (-not (Guard-Check 'after formime')) { Bail '  formime: the original moved during the run' }
    if ($Control) {
      if ($gv -ne '') {
        Bail ('  CONTROL: no context was requested on the app window and no composition was attempted, yet gamma ' +
              'carries [' + $gv + '] -- the positive''s gamma reading would not be the composition (B(delta)=[' +
              $dv + '] C(alpha)=[' + $av + ']) (CONTROL exit 1)')
      }
      Bail ('  CONTROL: gamma is empty with the composition left out, so the positive''s gamma reading can only be ' +
            'the composition; B(delta)=[' + $dv + '] C(alpha)=[' + $av + '] (CONTROL exit 1)')
    }
    if (-not $gv.StartsWith($wantU)) {
      # Two different findings behind one red, and the message has to say which one it is: either the
      # ladder got no context (nothing was ever composed -- the IMM32 answers above are the reading),
      # or a context existed and the committed string did not reach the field.
      $why = if ($aDriven) {
        'a context was obtained and the composition was sent, and gamma=['
      } else {
        'no input context was obtained on the app window, so no composition was ever sent (the ladder ' +
        'above is the reading) -- gamma=['
      }
      Bail ('  formime: the composition route did not land -- ' + $why + $gv + '], and it had to begin ' +
            'with [' + $wantU + ']; B(delta)=[' + $dv + '] C(alpha)=[' + $av + ']')
    }
    if ($gv -ne $wantU) {
      Bail ('  formime: the composition landed, but gamma also carries [' + $gv.Substring($wantU.Length) +
            '] -- another route''s characters reached gamma as well, so the three readings are not ' +
            'separated (B(delta)=[' + $dv + '] C(alpha)=[' + $av + '])')
    }
    Say ('  formime: the composition commit put [' + $wantU + '] into gamma and the save wrote it; beside it B(delta)=[' +
         $dv + '] C(alpha)=[' + $av + ']')
  }
  'formcombo' {
    # The combo box, three rounds, each ended by Ctrl+S and by the file's own answer --
    # because "the popup painted" and "the value persisted" are different claims:
    #   round 1  a press on the field's own dropdown button opens a list PDFium paints
    #            itself; its rows are found as dark ink bands under the field, the second
    #            one is pressed, and the saved file must answer with the second option;
    #   round 2  the same list opened with Alt+Down (the product's keyboard route), the
    #            third row pressed -- a different option on purpose, so a file that still
    #            held round 1's answer would fail this round rather than pass it;
    #   round 3  the editable combo: a click, a made-up word typed, Ctrl+S, read back.
    # The ink under the field is the geometric half: with the list closed that region is
    # empty because the sample leaves it empty (pdfprobe.cpp's comment says why), so the
    # difference is the popup and nothing else. -Control presses no dropdown button and no
    # key: it focuses the field, presses Ctrl+S, and still expects round 1's answer, so it
    # exits 1.
    Form-TitleCheck 'formcombo'
    Read-FormFields $script:guardCopy 'formcombo-before' | Out-Null
    $choice = Field-At 'choice' 'formcombo'
    $typeable = Field-At 'typeable' 'formcombo'
    if ((UForm $choice.value) -ne '') {
      Bail ('  formcombo: choice starts at [' + (UForm $choice.value) + '], not empty')
    }
    $choiceOptions = @(Option-Labels 'choice' 'formcombo')
    if ($choiceOptions.Count -lt 4) {
      Bail ('  formcombo: the file carries ' + $choiceOptions.Count + ' option(s) for choice, not the four the sample writes')
    }
    $sel0 = @(Selected-Labels 'choice' 'formcombo')
    if ($sel0.Count -ne 0) {
      Bail ('  formcombo: choice starts with [' + ($sel0 -join ', ') + '] selected, though the sample writes an empty value')
    }
    $shot = Zoom-WholePage 'formcombo'
    $mField = Map-Rect $shot $choice.l $choice.b $choice.r $choice.t
    Assert-InCrop $shot $mField 'formcombo the combo field'
    $mBelow = Map-Rect $shot $choice.l ($choice.b - 110) $choice.r ($choice.b - 2)
    $inkClosed = Region-Ink $shot $mBelow
    Say ('  formcombo: ink below the closed combo: ' + $inkClosed)
    if ($Control) {
      Say '  CONTROL: the dropdown button is not pressed; the field is focused and Ctrl+S is pressed'
      Click-Field $shot $choice 'formcombo-control'
      if (Wait-Star $app.Id $true 8) {
        Bail '  CONTROL: the caption grew the unsaved-edits star from a focus change alone -- nothing was picked'
      }
      $sv = Save-AndRead 'formcombo' $true
      Read-FormFields $script:guardCopy 'formcombo-control-after' | Out-Null
      $c = Field-At 'choice' 'formcombo'
      Say ('  CONTROL: save line=' + $sv.sawMsg + ' copy moved=' + $sv.moved + '; choice reads [' + (UForm $c.value) + ']')
      if ($sv.moved) {
        Bail '  CONTROL: the copy moved although no value was picked -- a moved file would not be evidence of the pick'
      }
      if ((UForm $c.value) -ne '') {
        Bail ('  CONTROL: choice came back [' + (UForm $c.value) + '] with the dropdown never opened')
      }
      Bail '  CONTROL: with the dropdown unpressed the value stayed empty and Ctrl+S changed nothing (CONTROL exit 1)'
    }
    # ---- round 1: the dropdown button ----
    $btn = Map-Rect $shot ($choice.r - 3) ($choice.b + 3) ($choice.r - 1) ($choice.t - 3)
    Click-Mapped $shot $btn 'formcombo-open' 'the dropdown button'
    Start-Sleep -Milliseconds 900
    $shotOpen = Page-Shot 'formcombo-open-list'
    $mBelowOpen = Map-Rect $shotOpen $choice.l ($choice.b - 110) $choice.r ($choice.b - 2)
    $inkOpen = Region-Ink $shotOpen $mBelowOpen
    $bands = @(Ink-Bands $shotOpen $mBelowOpen 'formcombo-open-list')
    Say ('  formcombo: ink below the open combo: ' + $inkOpen + ' against ' + $inkClosed + ' closed')
    if ($inkOpen -le ($inkClosed + 100)) {
      Bail ('  formcombo: the press on the dropdown button painted ' + ($inkOpen - $inkClosed) +
            ' px below the field (closed ' + $inkClosed + ', open ' + $inkOpen + ') -- the list is not on screen')
    }
    if ($bands.Count -lt 4) {
      Bail ('  formcombo: the open list reads ' + $bands.Count + ' row(s) of dark ink under the field while the file ' +
            'carries ' + $choiceOptions.Count + ' options -- clicking "the second row" would not be a click on the second option')
    }
    Say ('  formcombo: pressing the second row; the file''s second option is [' + $choiceOptions[1] + ']')
    Click-Band $shotOpen $mBelowOpen $bands[1] 'formcombo-pick' 'the second row of the open list'
    if (-not (Wait-Star $app.Id $true)) {
      Bail ('  formcombo: the caption never grew the star after picking a row; it reads [' + (Window $app.Id).Current.Name + ']')
    }
    $shotPicked = Page-Shot 'formcombo-picked'
    $mBelowPicked = Map-Rect $shotPicked $choice.l ($choice.b - 110) $choice.r ($choice.b - 2)
    $inkPicked = Region-Ink $shotPicked $mBelowPicked
    Say ('  formcombo: ink below the field after the pick: ' + $inkPicked + ' (closed ' + $inkClosed + ')')
    if ($inkPicked -gt ($inkClosed + 60)) {
      Bail ('  formcombo: the list is still painted below the field (' + $inkPicked + ' px, closed ' + $inkClosed +
            ') after a row was pressed -- the press did not close it')
    }
    Click-Blank 'formcombo'
    $sv = Save-AndRead 'formcombo' $true
    Check-Save $sv 'formcombo'
    Read-FormFields $script:guardCopy 'formcombo-after' | Out-Null
    $c = Field-At 'choice' 'formcombo'
    Say ('  read back: choice=[' + (UForm $c.value) + ']')
    if ((UForm $c.value) -ne $choiceOptions[1]) {
      Bail ('  formcombo: choice came back [' + (UForm $c.value) + '], not the row that was pressed [' +
            $choiceOptions[1] + ']')
    }
    # ---- round 2: the keyboard route, Alt+Down ----
    # A fresh picture because the save reopened the document and the zoom is fit-width again.
    # The field is clicked first: Alt+Down is the product's answer only while that combo holds
    # the keyboard (backend keyPressed reads the focused annot).
    $shot2 = Zoom-WholePage 'formcombo-alt'
    Click-Field $shot2 $choice 'formcombo-alt-focus'
    $shotFocus = Page-Shot 'formcombo-alt-focused'
    $mBelowFocus = Map-Rect $shotFocus $choice.l ($choice.b - 110) $choice.r ($choice.b - 2)
    $inkFocus = Region-Ink $shotFocus $mBelowFocus
    Say ('  formcombo: ink below the field after the click that focuses it: ' + $inkFocus)
    Press-KeyAlt 0x28 'formcombo-alt'
    Start-Sleep -Milliseconds 900
    $shotAlt = Page-Shot 'formcombo-alt-open'
    $mBelowAlt = Map-Rect $shotAlt $choice.l ($choice.b - 110) $choice.r ($choice.b - 2)
    $inkAlt = Region-Ink $shotAlt $mBelowAlt
    $bandsAlt = @(Ink-Bands $shotAlt $mBelowAlt 'formcombo-alt-open')
    Say ('  formcombo: Alt+Down -> ink below the field ' + $inkAlt + ' against ' + $inkFocus + ' before it')
    if ($inkAlt -le ($inkFocus + 100)) {
      Bail ('  formcombo: Alt+Down painted ' + ($inkAlt - $inkFocus) + ' px below the focused combo (' +
            $inkFocus + ' -> ' + $inkAlt + ') -- the list did not open')
    }
    if ($bandsAlt.Count -lt 4) {
      Bail ('  formcombo: the list Alt+Down opened reads ' + $bandsAlt.Count + ' row(s) while the file carries ' +
            $choiceOptions.Count + ' options')
    }
    Say ('  formcombo: pressing the third row; the file''s third option is [' + $choiceOptions[2] + ']')
    Click-Band $shotAlt $mBelowAlt $bandsAlt[2] 'formcombo-alt-pick' 'the third row of the list Alt+Down opened'
    if (-not (Wait-Star $app.Id $true)) {
      Bail ('  formcombo: the caption never grew the star after the Alt+Down pick; it reads [' + (Window $app.Id).Current.Name + ']')
    }
    Click-Blank 'formcombo-alt'
    $sv2 = Save-AndRead 'formcombo-alt' $true
    Check-Save $sv2 'formcombo-alt'
    Read-FormFields $script:guardCopy 'formcombo-alt-after' | Out-Null
    $c2 = Field-At 'choice' 'formcombo-alt'
    Say ('  read back: choice=[' + (UForm $c2.value) + ']')
    if ((UForm $c2.value) -ne $choiceOptions[2]) {
      Bail ('  formcombo: after Alt+Down and a row press, choice came back [' + (UForm $c2.value) +
            '], not [' + $choiceOptions[2] + '] -- a file still holding round 1''s answer means the pick did not land')
    }
    # ---- round 3: the editable combo takes a typed answer ----
    $shot3 = Zoom-WholePage 'formcombo-edit'
    $token = 'E' + [guid]::NewGuid().ToString('N').Substring(0, 6).ToUpper()
    Click-Field $shot3 $typeable 'formcombo-edit'
    $shotEdit = Page-Shot 'formcombo-edit-focused'
    $mBelowType = Map-Rect $shotEdit $typeable.l ($typeable.b - 110) $typeable.r ($typeable.b - 2)
    $inkBelowType = Region-Ink $shotEdit $mBelowType
    Say ('  formcombo: ink below the editable combo after the click: ' + $inkBelowType)
    Say ('  formcombo: typing [' + $token + '] into the editable combo')
    Type-Text $token 'formcombo-edit'
    if (-not (Wait-Star $app.Id $true)) {
      Bail ('  formcombo: the caption never grew the star after typing into the editable combo; it reads [' +
            (Window $app.Id).Current.Name + ']')
    }
    Click-Blank 'formcombo-edit'
    $sv3 = Save-AndRead 'formcombo-edit' $true
    Check-Save $sv3 'formcombo-edit'
    Read-FormFields $script:guardCopy 'formcombo-edit-after' | Out-Null
    $t = Field-At 'typeable' 'formcombo-edit'
    Say ('  read back: typeable=[' + (UForm $t.value) + ']')
    if ((UForm $t.value) -ne $token) {
      Bail ('  formcombo: the editable combo came back [' + (UForm $t.value) + '], not the typed [' + $token + ']')
    }
    if (-not (Guard-Check 'after formcombo')) { Bail '  formcombo: the original moved during the run' }
    Say ('  formcombo: the dropdown row [' + $choiceOptions[1] + '], Alt+Down row [' + $choiceOptions[2] +
         '] and the typed [' + $token + '] are all in the saved copy')
  }
  'formlist' {
    # The multi-select list box. A plain press on the first row and a Ctrl+press on the third,
    # then the file has to answer BOTH rows -- read off the per-option lines pdfprobe prints,
    # because the ordinary value read answers only the first selected entry (handoff-05 H1).
    # The control's second press is plain: the file then answers the last row only, so it exits
    # 1 -- which is what makes the Ctrl in the positive a cause rather than decoration.
    #
    # The rows are dark ink bands inside the field's own rectangle: PDFium lays the options out
    # itself, so the row height is measured off the picture rather than assumed.
    Form-TitleCheck 'formlist'
    Read-FormFields $script:guardCopy 'formlist-before' | Out-Null
    $list = Field-At 'list' 'formlist'
    $options = @(Option-Labels 'list' 'formlist')
    if ($options.Count -lt 4) {
      Bail ('  formlist: the file carries ' + $options.Count + ' option(s), not the four the sample writes')
    }
    $sel0 = @(Selected-Labels 'list' 'formlist')
    Say ('  formlist: before any click, ' + $sel0.Count + ' option(s) selected: [' + ($sel0 -join ', ') + ']')
    if ($sel0.Count -ne 0) {
      Bail ('  formlist: the list starts with [' + ($sel0 -join ', ') + '] selected, though the sample carries no value')
    }
    $shot = Zoom-WholePage 'formlist'
    $m = Map-Rect $shot $list.l $list.b $list.r $list.t
    Assert-InCrop $shot $m 'formlist the list box'
    $bands = @(Ink-Bands $shot $m 'formlist')
    if ($bands.Count -lt 3) {
      Bail ('  formlist: the list box paints ' + $bands.Count + ' row(s) of dark ink in its rectangle while the ' +
            'file carries ' + $options.Count + ' options -- there is no third row to Ctrl+click')
    }
    $ink0 = Region-Ink $shot $m
    Say ('  formlist: ink inside the list before any click: ' + $ink0)
    Say ('  formlist: pressing the first row; the file''s first option is [' + $options[0] + ']')
    Click-Band $shot $m $bands[0] 'formlist-first' 'the first row'
    if (-not (Wait-Star $app.Id $true)) {
      Bail ('  formlist: the caption never grew the star after the first press; it reads [' + (Window $app.Id).Current.Name + ']')
    }
    $shotB = Page-Shot 'formlist-after-first'
    $mB = Map-Rect $shotB $list.l $list.b $list.r $list.t
    $ink1 = Region-Ink $shotB $mB
    $bandsB = @(Ink-Bands $shotB $mB 'formlist-after-first')
    Say ('  formlist: ink inside the list after the first press: ' + $ink1 + ' (before ' + $ink0 + ')')
    if ($ink1 -le $ink0) {
      Bail ('  formlist: the first press painted nothing new inside the list (' + $ink0 + ' -> ' + $ink1 + ') -- no row was selected')
    }
    if ($bandsB.Count -lt 3) {
      Bail ('  formlist: after the first press the list reads ' + $bandsB.Count + ' row(s); the third row to press is not there')
    }
    if ($Control) {
      Say '  CONTROL: the second press is a plain click, with no Ctrl held'
      Click-Band $shotB $mB $bandsB[2] 'formlist-third-control' 'the third row, without Ctrl'
    } else {
      Say '  formlist: the second press is Ctrl+click on the third row'
      Click-Band $shotB $mB $bandsB[2] 'formlist-third' 'the third row' -Ctrl
    }
    $shotC = Page-Shot 'formlist-after-second'
    $mC = Map-Rect $shotC $list.l $list.b $list.r $list.t
    $ink2 = Region-Ink $shotC $mC
    Say ('  formlist: ink inside the list after the second press: ' + $ink2)
    Click-Blank 'formlist'
    $sv = Save-AndRead 'formlist' $true
    Check-Save $sv 'formlist'
    Read-FormFields $script:guardCopy 'formlist-after' | Out-Null
    $sel = @(Selected-Labels 'list' 'formlist')
    $dash = Field-At 'list' 'formlist'
    Say ('  read back: list=[' + (UForm $dash.value) + '], selected [' + ($sel -join ', ') + '] of [' +
         ($options -join ', ') + ']')
    $sawFirst = ($sel -contains $options[0])
    $sawThird = ($sel -contains $options[2])
    if ($Control) {
      if ($sel.Count -ne 1 -or -not $sawThird) {
        Bail ('  CONTROL: a plain second press left [' + ($sel -join ', ') + '] selected -- the control expected only [' +
              $options[2] + '], so this round proves nothing about the Ctrl in the positive (CONTROL exit 1)')
      }
      Bail ('  CONTROL: without Ctrl the second press replaced the first selection, and the file holds only [' +
            $options[2] + '] (CONTROL exit 1)')
    }
    if (-not $sawFirst -or -not $sawThird -or $sel.Count -ne 2) {
      Bail ('  formlist: after a press and a Ctrl+press the file answers [' + ($sel -join ', ') +
            '] -- both [' + $options[0] + '] and [' + $options[2] + '] were expected')
    }
    if (-not (Guard-Check 'after formlist')) { Bail '  formlist: the original moved during the run' }
    Say ('  formlist: [' + $options[0] + '] and [' + $options[2] + '] both survive the save')
  }
  'signedsave' {
    # handoff-07 I2 1: the incremental save of a signed document, read off the bytes it left behind.
    # sec.122 pinned it headless -- prefix identical, the /ByteRange spans hashing the same -- and no
    # Ctrl+S has ever been pressed on a signed file in front of this harness. The four receipts the
    # save modes already take (the line, the star, the copy moved, the original still there) say only
    # THAT a file was written. Two more say HOW: the copy's first N bytes are the file that was there
    # before the save (N = that earlier length), and the two spans its /ByteRange names hash to what
    # they hashed to before it. 'The copy moved' is deliberately not the interesting reading -- a
    # whole-file rewrite moves it too, and moving it is exactly what -Control makes happen.
    $nd = Signed-Needles
    $lang = InterfaceLanguage $app.Id
    Say ('  signedsave: interface language off the menu bar -> ' + $lang + ' (expected ' +
         $ExpectLanguage + ')')
    if ($ExpectLanguage -ne 'any' -and $lang -ne $ExpectLanguage) {
      Bail ('  signedsave: the window is speaking [' + $lang + '] but this round asked for [' +
            $ExpectLanguage + ']')
    }
    # The open-time line is read before anything is clicked. It is a timed message and a menu hover
    # replaces it outright (sec.114); the pointer was parked before the launch for this reason, and
    # sec.122 had to learn twice that a hover left over from the click that opened the file is enough
    # to lose it.
    $line = Wait-Status $app.Id $nd.signed
    if ($line -eq '') {
      Bail ('  signedsave: the status line never said the document carries a signature (it reads [' +
            (StatusName $app.Id) + ']) -- so nothing below can be about a signed document')
    }
    Say ('  signedsave: open line |' + $line.Replace("`r", ' ').Replace("`n", ' / ') + '|')
    if ($lang -eq 'en') {
      # handoff-07 I2's two sentences, read on the screen instead of the catalogue: the space
      # between them and the punctuation of a counted summary are the two things lupdate cannot
      # check. The sample is uncertified, so no certification sentence follows this line.
      if (-not (Check-EnglishSentences $line 'signedsave open line' $false)) {
        Bail ('  signedsave: the English status line breaks the sentence-spacing or punctuation ' +
              'rule (details above)')
      }
      Say '  signedsave: the English status line is spaced and carries no full-width punctuation'
    }
    $isOpenLine = $false
    foreach ($n in $nd.checking) { if ($line.Contains($n)) { $isOpenLine = $true } }
    if (-not $isOpenLine) {
      Say '  signedsave: the verdict had already replaced the open line, which is the one naming the file'
    } elseif (-not $line.Contains($script:guardCopyLeaf)) {
      Bail ('  signedsave: the open line does not name the copy this probe made [' +
            $script:guardCopyLeaf + ']')
    }
    Form-TitleCheck 'signedsave'
    Read-FormFields $script:guardCopy 'signedsave-before' 2 | Out-Null
    $tgt = Field-At 'name' 'signedsave'
    $sig = Field-At 'sig1' 'signedsave'
    # The value the generator put in, asked for before the click. A copy left over from an earlier
    # round would answer with that round's token, and every byte comparison after it would then be
    # about a file this run never saw.
    if ((UForm $tgt.value) -ne 'before') {
      Bail ('  signedsave: the text field reads [' + (UForm $tgt.value) + '] instead of the [before] ' +
            'the sample is built with, so this copy is not the file the ranges below describe')
    }
    if ($sig.type -ne 'SIGNATURE') {
      Bail ('  signedsave: the field the sample signs reads as ' + $sig.type + ', not SIGNATURE')
    }
    $bytes0 = File-Bytes $script:guardCopy 'signedsave-before'
    $n0 = $bytes0.Length
    $hash0 = Sha256-Of $bytes0
    $ranges0 = Byte-Ranges $bytes0 'signedsave-before'
    if ($ranges0.Count -ne 1) {
      Bail ('  signedsave: ' + $ranges0.Count + ' /ByteRange in the copy before the save and the ' +
            'generator writes exactly one signature, so this is not the file it made')
    }
    $covered0 = Covered-Bytes $bytes0 $ranges0[0] 'signedsave-before'
    if (-not $covered0) { Bail '  signedsave: the sample''s own /ByteRange points outside the file' }
    $coveredHash0 = Sha256-Of $covered0
    Say ('  signedsave: before -- length=' + $n0 + ' sha256=' + (Hex16 $hash0) + '; covered ' +
         $covered0.Length + ' bytes sha256=' + (Hex16 $coveredHash0))
    $shot = Zoom-WholePage 'signedsave'
    $m = Map-Rect $shot $tgt.l $tgt.b $tgt.r $tgt.t
    Say ('  field name ' + $tgt.type + ' user ' + $tgt.l + ',' + $tgt.b + '-' + $tgt.r + ',' + $tgt.t +
         ' -> bitmap ' + [int]$m.bx0 + ',' + [int]$m.by0 + '-' + [int]$m.bx1 + ',' + [int]$m.by1 +
         ' screen ' + [int]$m.sx + ',' + [int]$m.sy)
    $fd = Focus-Delta 'signedsave' $tgt
    # The channel before the claim: an unchanged rectangle means the field never took the keyboard,
    # and every byte read after that would be about a file nothing typed into.
    if ($fd.delta -lt 10) {
      Bail ('  signedsave: the click left the field''s own rectangle ' + $fd.delta + ' px short of a ' +
            'change, so it never took the keyboard -- an uncertified signature is refusing the field')
    }
    $token = 'S' + [guid]::NewGuid().ToString('N').Substring(0, 6).ToUpper()
    Say ('  signedsave: typing [' + $token + '] into name')
    Type-Text $token 'signedsave'
    Start-Sleep -Milliseconds 700
    if (-not (Wait-Star $app.Id $true)) {
      Bail ('  signedsave: the caption never grew the unsaved-edits star after typing; it reads [' +
            (Window $app.Id).Current.Name + ']')
    }
    Click-Blank 'signedsave'
    if ($Control) {
      # One extra edit ahead of the save, and it is the one edit that cannot be appended: a page turn
      # goes through qpdf, which rewrites the whole file, so the bytes the signature covers cannot
      # stay where they are. Every verdict below is kept, which is what makes this round exit 1.
      # The box it opens is also where sec.122's before-the-press sentence has never been read: this
      # is that round.
      Say '  CONTROL: rotating a page first; the prefix and the covered spans are still expected to hold'
      Rotate-CurrentPage $app.Id
      if (-not (Says-Any $script:rotateSummary $nd.invalidate)) {
        Bail ('  CONTROL: the rotate box on a signed file never said the save invalidates it -- the ' +
              'sentence is not on screen, so the verdict this mode adds is decoration')
      }
      Say '  CONTROL: the rotate box does say the signatures go; the byte verdicts are next'
    }
    $sv = Save-AndRead 'signedsave' $true
    Check-Save $sv 'signedsave'
    $bytes1 = File-Bytes $script:guardCopy 'signedsave-after'
    $n1 = $bytes1.Length
    $grew = ($n1 -ge $n0)
    $prefixHash = $(if ($grew) { Sha256-Prefix $bytes1 $n0 'signedsave-after' } else { '(shorter)' })
    $prefixHeld = ($prefixHash -eq $hash0)
    $ranges1 = Byte-Ranges $bytes1 'signedsave-after'
    # Every /ByteRange in the file has to be the one that was there before, not just the first. The
    # save measured here leaves TWO: PDFium appends its update and re-emits the signature dictionary in
    # it, so the xref the reader follows points at the copy in the appended revision and the one in the
    # prefix is the stale twin. Comparing only the first would let an update that rewrote the RANGES --
    # while leaving the old dictionary in front of it -- read as a pass, which is the vacuous half of
    # the receipt this mode exists to take.
    $sameRange = ($ranges1.Count -ge 1)
    $covered1 = @()
    foreach ($r in $ranges1) {
      if (($r -join ' ') -ne ($ranges0[0] -join ' ')) { $sameRange = $false }
      $c = Covered-Bytes $bytes1 $r 'signedsave-after'
      if ($c) { $covered1 += (Sha256-Of $c) }
    }
    # One hash per span pair, all of them required to equal the one taken before the save.
    $allCovered = $true
    foreach ($h in $covered1) { if ($h -ne $coveredHash0) { $allCovered = $false } }
    $coveredHash1 = $(if ($covered1.Count -gt 0) { $covered1[0] } else { '' })
    Say ('  signedsave: after -- length=' + $n1 + ' (was ' + $n0 + ') prefix sha256=' +
         (Hex16 $prefixHash) + ' against ' + (Hex16 $hash0) + '; /ByteRange still the same in all ' +
         $ranges1.Count + ' read(s)=' + $sameRange + '; covered sha256=' + (Hex16 $coveredHash1) +
         ' held for every span=' + $allCovered + ' (against ' + (Hex16 $coveredHash0) + ')')
    if ($prefixHeld -and $sameRange -and $allCovered) {
      if ($Control) {
        Bail ('  CONTROL: a page rotation left the first ' + $n0 + ' bytes and both covered spans ' +
              'exactly where they were, so the save appended an edit the file never took (CONTROL exit 1)')
      }
      if (-not $grew) {
        Bail ('  signedsave: the copy is ' + $n1 + ' bytes after a save that must have appended to ' +
              $n0 + ', so the line said it saved and the bytes did not grow')
      }
      Say ('  signedsave: a field-only save appended to the signed file -- the first ' + $n0 +
           ' bytes and the ' + $covered0.Length + ' bytes the signature covers are unchanged')
    } else {
      $why = @()
      if (-not $grew) { $why += ('the file shrank to ' + $n1 + ' from ' + $n0) }
      if (-not $prefixHeld) { $why += ('the first ' + $n0 + ' bytes are not the file that was there') }
      if (-not $sameRange) { $why += 'the /ByteRange numbers themselves moved' }
      if (-not $allCovered) { $why += 'the covered spans do not hash the same' }
      Bail ((('  signedsave: the save rewrote what it should have left alone -- ' + ($why -join '; ') +
             ' [' + $n0 + ' -> ' + $n1 + ' bytes]') + $(if ($Control) { ' (CONTROL exit 1)' } else { '' })))
    }
    Read-FormFields $script:guardCopy 'signedsave-readback' 2 | Out-Null
    $a = Field-At 'name' 'signedsave'
    Say ('  read back: name=[' + (UForm $a.value) + ']')
    if ((UForm $a.value) -notlike ('*' + $token + '*')) {
      Bail ('  signedsave: the copy answers [' + (UForm $a.value) + '] after the save, which does not ' +
            'carry the [' + $token + '] that was typed into it')
    }
    if (-not (Guard-Check 'after signedsave')) { Bail '  signedsave: the original moved during the run' }
  }
  'signedlock' {
    # handoff-07 I2 2: the two doors a certification signature shuts, judged where a reader meets them
    # -- the line at open, the accept button in a structure box, and a click on a field. sec.122 pinned
    # all three headless and sec.123 read the status line and one delete-box sentence off the packaged
    # copy; nothing here ever aimed a click at a P=1 field or asked a grey button whether it was grey.
    #
    # The three rounds are the same procedure against a different /P in the sample:
    #   1  the field refuses the click (no caret, no star, nothing written) and the box refuses the press
    #   2  the field takes it and the box still refuses, naming form filling as the only thing permitted
    #   0  signed but not certified: both doors open and no certification sentence appears anywhere
    # 0 is not the control -- it is the negative half, the reading that makes 'grey' in the other two a
    # claim about the signature rather than about a button this harness cannot light up. The control is
    # -Control, which builds the 0 document and keeps the verdicts it was handed: the accept-grey
    # assertion is the first one it can fail, so a grey button is shown to have a cause.
    #
    # The status line is CAPTURED at open and JUDGED at the end. It is a timed message and the delete
    # box needs a menu press, which replaces it (sec.114) -- so the reading has to be taken while it is
    # on screen, and the order of the verdicts is then free to put the accept button first.
    $nd = Signed-Needles
    $lang = InterfaceLanguage $app.Id
    Say ('  signedlock: interface language off the menu bar -> ' + $lang + ' (expected ' +
         $ExpectLanguage + ')')
    if ($ExpectLanguage -ne 'any' -and $lang -ne $ExpectLanguage) {
      Bail ('  signedlock: the window is speaking [' + $lang + '] but this round asked for [' +
            $ExpectLanguage + ']')
    }
    $wantP = [int]$script:expectP
    $grey = ($wantP -ge 1)          # structuralEditsRefused(): any DocMDP at all shuts all eight boxes
    # fillable() refuses at P=1 and at nothing else: P=0 is an ordinary signed document and P=2/3 are
    # the ones that say filling a field is permitted. Writing this as P>=2 would make the negative
    # round ask a fillable field to stay shut, and the round that exists to show the doors open would
    # read as a product defect.
    $fillable = ($wantP -ne 1)
    $line = Wait-Status $app.Id $nd.signed
    if ($line -eq '') {
      Bail ('  signedlock: the status line never said the document carries a signature (it reads [' +
            (StatusName $app.Id) + ']) -- so this run is not holding a signed document and no verdict ' +
            'below means anything')
    }
    Say ('  signedlock: open line |' + $line.Replace("`r", ' ').Replace("`n", ' / ') + '|')
    if ($lang -eq 'en') {
      # The two sentences sec.126 fixed for English, read off the bar: a P>=1 line carries the
      # certification sentence, which must arrive with its own leading space; a P=0 line has only
      # the opened/verdict sentence, and both must be free of full-width punctuation.
      $certWanted = ($wantP -ge 1)
      if (-not (Check-EnglishSentences $line 'signedlock open line' $certWanted)) {
        Bail ('  signedlock: the English status line breaks the sentence-spacing or punctuation ' +
              'rule (details above)')
      }
      Say ('  signedlock: the English status line is spaced and carries no full-width punctuation' +
           $(if ($certWanted) { ', and the certification sentence starts after a space' } else { '' }))
    }
    Form-TitleCheck 'signedlock'
    Read-FormFields $script:guardCopy 'signedlock-before' 2 | Out-Null
    $tgt = Field-At 'name' 'signedlock'
    if ((UForm $tgt.value) -ne 'before') {
      Bail ('  signedlock: the text field reads [' + (UForm $tgt.value) + '] instead of the [before] ' +
            'the sample is built with')
    }
    # ---- the structure box's button: the door, with a page actually named --------------------------
    # This is the rotate box rather than the delete box, and the reason is a measurement: the delete
    # box greys its accept until a range is named (deletepagesdialog.cpp wants `pagesToRemove > 0`),
    # and on this sample a range that names a page names the only page, which leaves nothing -- so
    # every range a reader can type greys it for a reason that has nothing to do with the signature.
    # The P=0 round found that out on screen: the button was grey on a document that refuses nothing.
    # A judgement that cannot distinguish those two documents is decoration (sec.99), so the button is
    # read where a page can be named and still leave a document: the rotate box, current page.
    $box = Rotate-Box $app.Id
    Say ('  signedlock: rotate accept |' + $box.accept.Current.Name + '| lit=' + $box.lit +
         ' with page 1 named -- this round expects grey=' + $grey)
    # First, because it is the verdict -Control is written to fail on.
    if ($grey -and $box.lit) {
      Bail ('  signedlock: on a document certified with P=' + $wantP + ' the rotate box lights its ' +
            'accept anyway' + $(if ($Control) { ' -- the sample it was built with certifies nothing (CONTROL exit 1)' } else { '' }))
    }
    if (-not $grey -and -not $box.lit) {
      Bail ('  signedlock: the rotate box greys its accept on a signed document that certifies ' +
            'nothing, with a page named and one page left standing -- the reader could not turn a ' +
            'page of a file that permits it')
    }
    if (-not (Says-Any $box.summary $nd.invalidate)) {
      Bail ('  signedlock: the rotate box on a signed file never said what the save costs the ' +
            'signature -- sec.122''s sentence has not reached the screen')
    }
    ClickCancel $box.dlg 'RotatePagesDialog' $app.Id
    # ---- the delete box: the sentences, and the button that means nothing here ---------------------
    $dlg = OpenDocumentDialog $app.Id @('Delete Pages', (CjStr 0x5220, 0x9664, 0x9875, 0x9762)) 'DeletePagesDialog'
    $summary = LongestText $dlg
    Say ('  signedlock: delete box (' + @(FindType $dlg $CT::Text).Count + ' Text element(s)): |' +
         $summary.Replace("`r", ' ').Replace("`n", ' / ') + '|')
    $ok = MatchOne $dlg $CT::Button @('Delete these pages', (CjStr 0x5220, 0x9664, 0x8FD9, 0x4E9B, 0x9875))
    if (-not $ok) {
      foreach ($b in @(FindType $dlg $CT::Button)) { Say ('    button |' + $b.Current.Name + '|') }
      Bail '  signedlock: no accept button among them in either language'
    }
    # Printed, never judged -- see the paragraph above: on a one-page document this button is grey
    # whether or not the file is certified, so its state would describe the arithmetic, not the door.
    Say ('  signedlock: delete accept |' + $ok.Current.Name + '| enabled=' + $ok.Current.IsEnabled +
         ' (printed only: nothing has been named, and on a one-page file a named range leaves nothing)')
    $hasP1 = Says-Any $summary $nd.certP1
    $hasP2 = Says-Any $summary $nd.certP2
    Say ('  signedlock: box says P1=' + $hasP1 + ' P2=' + $hasP2 + '  (the round is P=' + $wantP + ')')
    if ($wantP -eq 1 -and -not $hasP1) {
      Bail '  signedlock: a P=1 box refuses the press without saying the certification forbids every change'
    }
    if ($wantP -ge 2 -and -not $hasP2) {
      Bail '  signedlock: a P>=2 box refuses the press without naming form filling as the only thing permitted'
    }
    if ($wantP -eq 0 -and ($hasP1 -or $hasP2)) {
      Bail '  signedlock: the box warns about a certification signature on a document that carries none'
    }
    if (($wantP -ge 1) -and ($hasP1 -eq ($wantP -eq 2))) {
      # Both sentences on one box would mean the notice is not read off /P at all, and the two
      # sentences contradict each other.
      Bail ('  signedlock: the box carries both certification sentences on a P=' + $wantP + ' document')
    }
    if (-not (Says-Any $summary $nd.invalidate)) {
      Bail ('  signedlock: the delete box on a signed file never said what the save costs the ' +
            'signature')
    }
    ClickCancel $dlg 'DeletePagesDialog' $app.Id
    # ---- the field: the click, and what a key does after it --------------------------------------
    Zoom-WholePage 'signedlock' | Out-Null
    $fd = Focus-Delta 'signedlock' $tgt
    $hashBefore = Copy-Hash
    $token = 'L' + [guid]::NewGuid().ToString('N').Substring(0, 6).ToUpper()
    Say ('  signedlock: typing [' + $token + '] -- the field ' +
         $(if ($fillable) { 'is allowed to take it' } else { 'is not allowed to take anything at all' }))
    Type-Text $token 'signedlock'
    Start-Sleep -Milliseconds 700
    $star = Wait-Star $app.Id $true 8
    if ($fillable) {
      if ($fd.delta -lt 10) {
        Bail ('  signedlock: on a P=' + $wantP + ' document the click left the field''s rectangle ' +
              $fd.delta + ' px short of a change, so the field never took the keyboard')
      }
      if (-not $star) {
        Bail ('  signedlock: typing into a field a P=' + $wantP + ' certification permits grew no ' +
              'unsaved-edits star; it reads [' + (Window $app.Id).Current.Name + ']')
      }
      Say '  signedlock: the field took the click and the typing, and the caption grew the star'
      Click-Blank 'signedlock'
      $sv = Save-AndRead 'signedlock' $true
      Check-Save $sv 'signedlock'
      Read-FormFields $script:guardCopy 'signedlock-readback' 2 | Out-Null
      $a = Field-At 'name' 'signedlock'
      Say ('  read back: name=[' + (UForm $a.value) + ']')
      if ((UForm $a.value) -notlike ('*' + $token + '*')) {
        Bail ('  signedlock: after a P=' + $wantP + ' save the copy answers [' + (UForm $a.value) +
              '], which does not carry the [' + $token + '] that was typed')
      }
    } else {
      if ($fd.delta -gt 10) {
        Bail ('  signedlock: a field a P=1 signature forbids still changed by ' + $fd.delta +
              ' px inside its own rectangle when clicked -- the click took the keyboard')
      }
      Say ('  signedlock: the click changed the field''s rectangle by ' + $fd.delta + ' px, which is ' +
           'what a field that did not take the keyboard changes by')
      if ($star) {
        Bail ('  signedlock: typing into a refused field grew the unsaved-edits star -- the refusal ' +
              'is only a cursor and not a door')
      }
      if ((Copy-Hash) -ne $hashBefore) {
        Bail '  signedlock: the copy on disk moved while a P=1 document refused every keystroke'
      }
      Say '  signedlock: no star and no bytes moved, which is what a refused field leaves behind'
    }
    # ---- the open line's second half, judged off the captured reading ------------------------------
    $lineP1 = Says-Any $line $nd.certP1
    $lineP2 = Says-Any $line $nd.certP2
    Say ('  signedlock: open line says P1=' + $lineP1 + ' P2=' + $lineP2 + '  (the round is P=' +
         $wantP + ')')
    if ($wantP -eq 1 -and -not $lineP1) {
      Bail '  signedlock: the line at open does not tell the reader the document refuses every change'
    }
    if ($wantP -ge 2 -and -not $lineP2) {
      Bail '  signedlock: the line at open does not tell the reader only form filling is permitted'
    }
    if ($wantP -eq 0 -and ($lineP1 -or $lineP2)) {
      Bail '  signedlock: the line at open warns about a certification the file does not carry'
    }
    # Only the open line names the file; the verdict that replaces it a moment later (sec.126) does
    # not, and it carries the same certification sentence the checks above just read.
    $isOpenLine = $false
    foreach ($n in $nd.checking) { if ($line.Contains($n)) { $isOpenLine = $true } }
    if (-not $isOpenLine) {
      Say '  signedlock: the verdict had already replaced the open line, which is the one naming the file'
    } elseif (-not $line.Contains($script:guardCopyLeaf)) {
      Bail ('  signedlock: the open line does not name the copy this probe made [' +
            $script:guardCopyLeaf + ']')
    }
    if (-not (Guard-Check 'after signedlock')) { Bail '  signedlock: the original moved during the run' }
    Say ('  signedlock: P=' + $wantP + ' judged where the reader meets it' +
         $(if ($Control) { ' -- and the accept button proved it is a verdict that can fail' } else { '' }))
  }

  'signaturepanel' {
    # handoff-08 J1: the sidebar's signatures page. Section 126 pinned the four verdicts, the
    # revision sentence and the summary headless; this is the first round that asks the panel to
    # DRAW them. What is judged, in the order a reader meets it: the tab exists, switching to it
    # selects it, each top-level row's text carries the verdict that file must produce here, its
    # expanded details carry the file's own revision count (or nothing when there were no
    # revisions), clicking it puts it in the tree's selection, and a picture of the tab strip and
    # the panel is kept. -Sample none is the negative: an unsigned file has no signatures tab, and
    # the other three tabs are asserted first so a sidebar that never opened cannot pass for it.
    #
    # The interface is read off the menu bar rather than assumed: every needle list here is
    # bilingual, so a round that asked for English and got Chinese would still match and report a
    # screen it never read (sec.92).
    $sn = SignatureNeedles
    $win = $null
    for ($k = 1; $k -le 10 -and -not $win; $k++) {
      Start-Sleep -Milliseconds 500
      $win = Window $app.Id
    }
    if (-not $win) { Bail '  no window' }
    $hwnd = [IntPtr]$win.Current.NativeWindowHandle
    [W32]::SwitchToThisWindow($hwnd, $true)
    Start-Sleep -Milliseconds 500
    Say ('  foreground before the click [' + [W32]::Text([W32]::GetForegroundWindow()) + ']')
    $wr = $win.Current.BoundingRectangle
    [W32]::Click([int]($wr.X + $wr.Width / 2), [int]($wr.Y + 12))   # raise it by its own title bar
    Start-Sleep -Milliseconds 800
    Say ('  foreground after the click  [' + [W32]::Text([W32]::GetForegroundWindow()) + ']')

    $lang = InterfaceLanguage $app.Id
    Say ('  menu bar [' + ((MenuBarNames $app.Id) -join '| |') + '] -> interface [' + $lang + ']')
    if ($ExpectLanguage -ne 'any' -and $lang -ne $ExpectLanguage) {
      Bail ('  the window is speaking [' + $lang + '] but this round asked for [' + $ExpectLanguage +
            '], so a match here would not be about the screen this round meant to read')
    }
    $readLang = $lang
    if ($readLang -eq 'unknown') { $readLang = $ExpectLanguage }
    $tabNeedles = Pick-Needles $sn.tab $readLang

    # ---- the sidebar -------------------------------------------------------------------------
    # Same route as outlinetree (its copy is intertwined with its tree read and was left alone):
    # the row's TogglePattern state is read on both sides, the real click goes first because it is
    # the channel that has worked since sec.34, and an open menu is collapsed first because asking
    # the bar for a menu that is already open closes it (sec.56).
    $tabs = @(FindType (Window $app.Id) $CT::TabItem)
    if (-not (SidebarUpLoose $tabs)) {
      Say ('  the sidebar is closed (' + $tabs.Count + ' tab item(s)); opening it through View')
      foreach ($way in @('real click', 'toggle-pattern')) {
        foreach ($bar in @(FindType (Window $app.Id) $CT::MenuBar)) {
          foreach ($t in @($bar.FindAll($TS::Children, $ANY))) {
            $xp = Pat $t 'ExpandCollapse'
            if ($xp -and $xp.Current.ExpandCollapseState.ToString() -eq 'Expanded') {
              try { $xp.Collapse(); Say ('  collapsed an open [' + $t.Current.Name + '] first') } catch { }
            }
          }
        }
        Start-Sleep -Milliseconds 400
        TopMenu $app.Id @('View', (CjStr 0x67E5, 0x770B)) | Out-Null
        $row = RowAny $app.Id @('Navigation Sidebar', (CjStr 0x5BFC, 0x822A, 0x4FA7, 0x680F))
        if (-not $row) { Say ('  ' + $way + ': no Navigation Sidebar row in the View menu'); continue }
        $tg = Pat $row 'Toggle'
        $beforeState = '(no TogglePattern)'
        if ($tg) { $beforeState = $tg.Current.ToggleState.ToString() }
        $rr = $row.Current.BoundingRectangle
        Say ('  ' + $way + ': aiming at |' + $row.Current.Name + '| at ' + [int]$rr.X + ',' +
             [int]$rr.Y + ' enabled=' + $row.Current.IsEnabled + ' toggle=' + $beforeState)
        if ($way -eq 'toggle-pattern') {
          if (-not $tg) { Say '  no TogglePattern to call'; continue }
          try { $tg.Toggle() } catch { Say ('  Toggle threw: ' + $_.Exception.Message) }
        } else {
          ClickAt $row
        }
        for ($k = 1; $k -le 8; $k++) {
          Start-Sleep -Milliseconds 500
          $tabs = @(FindType (Window $app.Id) $CT::TabItem)
          if (SidebarUpLoose $tabs) { break }
        }
        Say ('  ' + $way + ' gave ' + $tabs.Count + ' tab item(s)')
        if (SidebarUpLoose $tabs) { break }
      }
    }
    if (-not (SidebarUpLoose $tabs)) {
      Dump-Names (Window $app.Id) 'the sidebar did not come up; every name this window has'
      Bail 'the sidebar did not come up -- the names printed above are what this window offered'
    }

    $tabs = @(FindType (Window $app.Id) $CT::TabItem)
    Say ('  ' + $tabs.Count + ' tab item(s) on screen:')
    $ti = 0
    foreach ($t in $tabs) {
      $ti++
      $nm = '(no name)'
      try { $nm = $t.Current.Name } catch { }
      # A hidden QTabBar page keeps its TabItem in the tree, and on this Qt build asking it for a
      # rectangle THROWS -- a statement-terminating error that silently swallowed the whole printed
      # line (the first negative run counted five tabs and listed four). Reading it has to be a
      # caught step of its own, and "hidden" is then a reading rather than a missing line.
      try {
        $tr = $t.Current.BoundingRectangle
        $off = $t.Current.IsOffscreen
        Say ('    tab ' + $ti + ' |' + $nm + '| at ' + [int]$tr.X + ',' + [int]$tr.Y + ' ' +
             [int]$tr.Width + 'x' + [int]$tr.Height + ' selected=' + (TabSelected $t) +
             ' offscreen=' + $off)
      } catch {
        Say ('    tab ' + $ti + ' |' + $nm + '| has no on-screen rectangle (hidden page)')
      }
    }
    if (-not (SidebarComplete $tabs $readLang)) {
      Bail ('  the sidebar is missing at least one of its three ordinary tabs, so "the signatures ' +
            'tab is not there" would be a claim about a sidebar that is not fully up')
    }
    Say '  the sidebar is up with its three ordinary tabs'

    $sigTab = Find-TabByName $tabs $tabNeedles

    if ($Sample -eq 'none') {
      # ---- the negative half ---------------------------------------------------------------
      if (-not $Control) {
        if ($sigTab -and (TabOnScreen $sigTab)) {
          Bail ('  an unsigned document still shows a |' + $sigTab.Current.Name + '| tab -- the ' +
                'page is supposed to be there only for a file that carries signatures')
        }
        if ($sigTab) {
          Say '  a signatures tab is in the tree but has no on-screen rectangle -- a hidden page, not a page'
        }
        Say '  the unsigned document shows no signatures tab on screen, and the other three are up'
        $sideTabs = @($tabs | Where-Object { TabOnScreen $_ -and (SidebarTabNames) -contains $_.Current.Name })
        Save-WidgetPicture $hwnd ($Sample + '-' + $readLang) @($sideTabs) $false
        Say '  the negative half holds: no signatures tab on a document with no signature'
        break
      }
      if (-not ($sigTab -and (TabOnScreen $sigTab))) {
        Bail ('  CONTROL: an unsigned document was expected to show a signatures tab on screen and ' +
              'shows none -- the negative assertion is a reading that can fail (CONTROL exit 1)')
      }
      Bail ('  an unsigned document shows a |' + $sigTab.Current.Name + '| tab')
    }

    # ---- the tab ----------------------------------------------------------------------------
    if (-not (TabOnScreen $sigTab)) {
      Bail '  the signatures tab is in the tree but has no on-screen rectangle -- a signed document cannot have a hidden page'
    }
    Say ('  signatures tab |' + $sigTab.Current.Name + '| selected=' + (TabSelected $sigTab))
    $skipTab = ($Control -and $Sample -eq 'x509-rsa-sha1')
    if ($skipTab) {
      Say '  CONTROL: not switching to the signatures page; the read below is expected to fail'
    } else {
      ClickAt $sigTab
      Start-Sleep -Milliseconds 800
      $tabs = @(FindType (Window $app.Id) $CT::TabItem)
      $sigTab = Find-TabByName $tabs $tabNeedles
    }
    $sel = $null
    if ($sigTab) { $sel = TabSelected $sigTab }
    if ($null -eq $sel -and $sigTab) {
      # Some Qt builds put selection on the tab STRIP rather than on each TabItem; ask that too
      # before calling the switch unproven, and print which channel answered.
      $parent = $null
      try { $parent = [System.Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($sigTab) } catch { }
      if ($parent) {
        $sp = Pat $parent 'Selection'
        if ($sp) {
          try {
            $names = @($sp.Current.GetSelection() | ForEach-Object { $_.Current.Name })
            Say ('  the tab strip''s own selection holds |' + ($names -join '| |') + '|')
            foreach ($n in $names) { if ($tabNeedles -contains $n) { $sel = $true } }
          } catch { Say ('  the tab strip''s SelectionPattern threw: ' + $_.Exception.Message) }
        }
      }
    }
    Say ('  the signatures tab reports selected=' + $sel + ' after the switch')
    if ($sel -ne $true) {
      Bail ('  the signatures tab is not the selected page after the click (it reports [' + $sel +
            '])' + $(if ($skipTab) { ' -- the tab was never clicked (CONTROL exit 1)' } else { '' }))
    }

    # ---- the tree ---------------------------------------------------------------------------
    # Scoped to the tab strip first: the window also has the document tab strip, and its pages are
    # not this panel. Every candidate is printed, because "the first tree" was a reading outlinetree
    # could take when only one existed and is a guess now.
    $win = Window $app.Id
    $parent = $null
    try { if ($sigTab) { $parent = [System.Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($sigTab) } } catch { }
    if ($parent) {
      Say ('  the signatures tab''s parent is ' + $parent.Current.ControlType.ProgrammaticName +
           ' class=' + $parent.Current.ClassName + ' name=|' + $parent.Current.Name + '|')
    }
    $trees = @()
    if ($parent) { $trees = @(FindType $parent $CT::Tree) }
    if ($trees.Count -eq 0) { $trees = @(FindType $win $CT::Tree) }
    Say ('  ' + $trees.Count + ' tree element(s) reachable from ' +
         $(if ($parent) { 'the tab strip' } else { 'the window' }) + ':')
    foreach ($t in $trees) {
      $tr = $t.Current.BoundingRectangle
      Say ('    tree at ' + [int]$tr.X + ',' + [int]$tr.Y + ' ' + [int]$tr.Width + 'x' + [int]$tr.Height)
    }
    $tree = $null
    foreach ($t in $trees) {
      $tr = $t.Current.BoundingRectangle
      if ($tr.Width -gt 0 -and $tr.Height -gt 0) { if (-not $tree) { $tree = $t } }
    }
    if (-not $tree) {
      Dump-Names $(if ($parent) { $parent } else { $win }) 'no tree with a rectangle; what the sidebar offers'
      Bail ('  no tree element on the signatures page' +
            $(if ($skipTab) { ' -- the tab was never clicked (CONTROL exit 1)' } else { '' }))
    }
    $rootRows = @($tree.FindAll($TS::Children, (New-Object $PC($AE::ControlTypeProperty, $CT::TreeItem))))
    $allRows = @(FindType $tree $CT::TreeItem)
    $gp = Pat $tree 'Grid'
    if ($gp) { Say ('  GridPattern rows=' + $gp.Current.RowCount + ' columns=' + $gp.Current.ColumnCount) }
    $tp = Pat $tree 'Table'
    if ($tp) { Say ('  TablePattern rows=' + $tp.Current.RowCount) }
    $treeSel = Pat $tree 'Selection'
    if ($treeSel) {
      try { Say ('  SelectionPattern holds ' + $treeSel.Current.GetSelection().Length + ' item(s)') } catch { }
    }
    Say ('  TreeItem root children=' + $rootRows.Count + ' descendants=' + $allRows.Count)
    if ($rootRows.Count -eq 0) {
      Dump-Names $tree 'the tree has no top-level row; what is in it'
      Bail ('  no signature row on the signatures page (the tab was ' +
            $(if ($skipTab) { 'never clicked' } else { 'clicked' }) + ')' +
            $(if ($skipTab) { ' (CONTROL exit 1)' } else { '' }))
    }

    # ---- the rows ---------------------------------------------------------------------------
    # UIA's ExpandCollapsePattern.Expand() is tried first, then a real click on the branch
    # indicator: on this tree the pattern call leaves the state Collapsed (measured on
    # appended-one), and the detail children only exist once the row is open. The row's UIA
    # rectangle starts after the branch indent (row x = tree x + 31 on a 16-px icon theme), so the
    # chevron is aimed at the tree's own left edge, not at the row rectangle. Rows are opened
    # bottom-up so expanding one never moves the rectangles of the ones still to be clicked.
    #
    # After opening, the tree's own element references go stale (the pre-expand row element then
    # answers an empty name), so everything below is read again from a fresh FindAll. On this Qt
    # build the detail items are exposed as SIBLINGS in document order, not as children of the
    # row, so a row's details are the items between its own name and the next row's name.
    $tr = $tree.Current.BoundingRectangle
    $rowNames = @($rootRows | ForEach-Object { $_.Current.Name.Trim() })
    $openedByUia = 0
    $openedByClick = 0
    for ($i = $rootRows.Count - 1; $i -ge 0; $i--) {
      $ex = Pat $rootRows[$i] 'ExpandCollapse'
      $state0 = '(none)'
      if ($ex) { try { $state0 = $ex.Current.ExpandCollapseState.ToString() } catch { } }
      if ($ex -and $state0 -ne 'Expanded') {
        try { $ex.Expand() } catch { Say ('  Expand threw: ' + $_.Exception.Message) }
      }
      $state1 = $state0
      if ($ex) { try { $state1 = $ex.Current.ExpandCollapseState.ToString() } catch { } }
      if ($state1 -ne 'Expanded') {
        $rrX = $rootRows[$i].Current.BoundingRectangle
        $bx = [int]($tr.X + 11)
        $by = [int]($rrX.Y + $rrX.Height / 2)
        Say ('  row ' + ($i + 1) + ': UIA Expand left it ' + $state1 +
             '; clicking the branch indicator at ' + $bx + ',' + $by)
        [W32]::Click($bx, $by)
        Start-Sleep -Milliseconds 500
        $openedByClick++
        if ($ex) { try { $state1 = $ex.Current.ExpandCollapseState.ToString() } catch { } }
      } elseif ($ex) {
        $openedByUia++
      }
      Say ('  row ' + ($i + 1) + ' expand state ' + $state0 + ' -> ' + $state1)
    }
    Start-Sleep -Milliseconds 500
    Say ('  opened ' + $openedByUia + ' row(s) through UIA Expand and ' + $openedByClick +
         ' through the branch indicator')

    $afterRows = @(FindType $tree $CT::TreeItem)
    Say ('  tree holds ' + $afterRows.Count + ' TreeItem(s) after opening:')
    foreach ($r in $afterRows) { Say ('    item |' + $r.Current.Name.Trim() + '|') }

    $rowText = @()
    $rowKids = @()
    $rowEls = @()
    $nextRow = 0
    foreach ($it in $afterRows) {
      $n = $it.Current.Name.Trim()
      if ($nextRow -lt $rowNames.Count -and $n -eq $rowNames[$nextRow]) {
        $rowText += $n
        $rowKids += ,@()
        $rowEls += $it
        $nextRow++
      } elseif ($rowText.Count -gt 0) {
        $rowKids[$rowText.Count - 1] = @($rowKids[$rowText.Count - 1]) + $n
      } else {
        Say ('  an item before the first signature row: |' + $n + '|')
      }
    }
    if ($nextRow -ne $rowNames.Count) {
      Dump-Names $tree 'the opened tree no longer matches the rows it showed before opening'
      Bail ('  read ' + $nextRow + ' of ' + $rowNames.Count + ' signature row(s) after opening')
    }
    for ($i = 0; $i -lt $rowText.Count; $i++) {
      Say ('  row |' + $rowText[$i] + '| with ' + $rowKids[$i].Count + ' detail line(s)')
      foreach ($d in $rowKids[$i]) { Say ('    detail |' + $d + '|') }
    }
    if ($rowEls.Count -gt 0) {
      $freshKids = @($rowEls[0].FindAll($TS::Children, (New-Object $PC($AE::ControlTypeProperty, $CT::TreeItem))))
      Say ('  the first row element holds ' + $freshKids.Count + ' TreeItem child(ren) of its own')
    }

    $exp = SignatureExpectation $Sample
    if (-not $exp) { Bail ('  no expectation table entry for -Sample [' + $Sample + ']') }
    $wantRows = [int]$exp.rows
    $wantVerdict = [string]$exp.verdict
    $wantInc = [int]$exp.inc
    if ($Control) {
      # One deliberately wrong reading per sample, each keeping the verdict it was written against,
      # so every assertion below is shown to be able to fail (sec.99).
      if ($Sample -eq 'valid') { $wantInc = 1 }
      if ($Sample -eq 'narrowed') { $wantRows = 2 }
      if ($Sample -eq 'tampered') { $wantVerdict = 'cert' }
      if ($Sample -eq 'appended-one') { $wantInc = 2 }
    }
    Say ('  expecting ' + $wantRows + ' row(s), verdict [' + $wantVerdict + '], ' + $wantInc +
         ' incremental-update line(s)' + $(if ($Control) { '  (CONTROL)' } else { '' }))

    if ($rowText.Count -ne $wantRows) {
      Bail ('  the panel shows ' + $rowText.Count + ' top-level row(s), not ' + $wantRows +
            $(if ($Control -and $Sample -eq 'narrowed') { ' (CONTROL exit 1)' } else { '' }))
    }

    $verdictPair = $null
    switch ($wantVerdict) {
      'cert'    { $verdictPair = $sn.cert }
      'changed' { $verdictPair = $sn.changed }
      'cannot'  { $verdictPair = $sn.cannot }
    }
    $verdictNeedles = Pick-Needles $verdictPair $readLang
    $first = $rowText[0]
    $verdictSaid = $false
    foreach ($n in $verdictNeedles) { if ($first -like ('*' + $n + '*')) { $verdictSaid = $true } }
    Say ('  the first row carries the expected verdict=' + $verdictSaid)
    if (-not $verdictSaid) {
      Bail ('  the first row reads |' + $first + '| and the sample [' + $Sample + '] must read [' +
            ($verdictNeedles -join ' / ') + ']' +
            $(if ($Control -and $Sample -eq 'tampered') { ' (CONTROL exit 1)' } else { '' }))
    }

    $incNeedles = Pick-Needles $sn.incAny $readLang
    $incLines = @()
    foreach ($d in $rowKids[0]) { if (Says-Any $d $incNeedles) { $incLines += $d } }
    Say ('  incremental-update line(s) on the first row: ' + $incLines.Count + '  expected ' +
         $wantInc)
    if ($wantInc -eq 0) {
      if ($incLines.Count -ne 0) {
        Bail ('  a file that was never appended to still says [' + $incLines[0] + ']' +
              $(if ($Control -and $Sample -eq 'valid') { ' (CONTROL exit 1)' } else { '' }))
      }
      Say '  the row says nothing about revisions after the signature, and the file has none'
    } else {
      if ($incLines.Count -ne 1) {
        Bail ('  the row carries ' + $incLines.Count + ' incremental-update line(s), expected exactly 1')
      }
      $incPair = $(if ($wantInc -eq 1) { $sn.inc1 } else { $sn.inc2 })
      $incNeedles2 = Pick-Needles $incPair $readLang
      Say ('  number check: line=[' + $incLines[0] + '] pair=' + $incPair.Count + ' [' +
           ($incPair -join ' / ') + '] needles=[' + ($incNeedles2 -join ' / ') +
           '] lang=' + $readLang + ' match=' + (Says-Any $incLines[0] $incNeedles2))
      if (-not (Says-Any $incLines[0] $incNeedles2)) {
        Bail ('  the row reads [' + $incLines[0] + '] instead of naming ' + $wantInc + ' update(s)' +
              $(if ($Control -and $Sample -eq 'appended-one') { ' (CONTROL exit 1)' } else { '' }))
      }
      Say ('  the row names exactly ' + $wantInc + ' incremental update(s) after the signature')
    }

    # ---- the click --------------------------------------------------------------------------
    # The sample is one page, so the page cannot move; what a press must do is select the row. The
    # blank click first is what makes "selected" a change rather than a state that was already true
    # before the interaction (sec.97's rule, applied to a selection).
    #
    # A press that reaches the row but leaves the selection empty is a PRODUCT defect, not a missed
    # click: this panel gives every row Qt::ItemIsEnabled alone (signaturepanel.cpp, setSignatures),
    # which also clears ItemIsSelectable, so Qt cannot select it however the press is delivered.
    # Both proof channels are printed -- an empty selection also reads exactly like a click that
    # landed on the page. The round keeps going through the picture and names the defect at the end,
    # so one product defect does not cost the other readings (or the picture) this mode exists for.
    $selNames0 = @()
    if ($treeSel) {
      try { $selNames0 = @($treeSel.Current.GetSelection() | ForEach-Object { $_.Current.Name }) } catch { }
    }
    Say ('  selection before any click: [' + ($selNames0 -join '| |') + ']')

    $tr = $tree.Current.BoundingRectangle
    $rr0 = $rowEls[0].Current.BoundingRectangle
    # "Blank" means below every visible item, not below the first row: after opening, the row's own
    # 30-px rectangle is followed by its detail lines, and a click in there would select a DETAIL
    # child (those are selectable even though the row is not) and become a baseline that was never
    # empty. The lowest item is looked up from the fresh list for the same reason.
    $lowest = $rr0.Y + $rr0.Height
    foreach ($it in $afterRows) {
      try {
        $ir = $it.Current.BoundingRectangle
        if ($ir.Height -gt 0 -and $ir.Y + $ir.Height -gt $lowest) { $lowest = $ir.Y + $ir.Height }
      } catch { }
    }
    $blankY = [int](($lowest + $tr.Y + $tr.Height) / 2)
    if ($blankY -lt ($tr.Y + $tr.Height - 8) -and $blankY -gt $lowest + 4) {
      [W32]::Click([int]($tr.X + $tr.Width / 2), $blankY)
      Start-Sleep -Milliseconds 350
      if ($treeSel) {
        try { $selNames0 = @($treeSel.Current.GetSelection() | ForEach-Object { $_.Current.Name }) } catch { }
      }
      Say ('  selection after a blank click inside the tree: [' + ($selNames0 -join '| |') + ']')
    } else {
      Say '  no blank space below the row inside the tree; the baseline is whatever it was'
    }
    # The row's UIA rectangle is as wide as the COLUMN, not as the viewport: this panel resizes its
    # one column to the longest line, so on a 262-px sidebar the row rectangle reads 569 px wide and
    # its own centre falls outside the tree (a click there lands on the page). Aim at the middle of
    # the part of the row the reader can actually see, and say so.
    $visL = [Math]::Max($rr0.X, $tr.X)
    $visR = [Math]::Min($rr0.X + $rr0.Width, $tr.X + $tr.Width)
    $cy = $rr0.Y + [Math]::Min($rr0.Height, $tr.Height) / 2
    Say ('  clicking the first row at ' + [int](($visL + $visR) / 2) + ',' + [int]$cy +
         ' (row rect ' + [int]$rr0.X + ',' + [int]$rr0.Y + ' ' + [int]$rr0.Width + 'x' +
         [int]$rr0.Height + ', tree ' + [int]$tr.X + ',' + [int]$tr.Y + ' ' + [int]$tr.Width +
         'x' + [int]$tr.Height + '; the row''s own centre ' +
         $(if ($rr0.X + $rr0.Width / 2 -gt $tr.X + $tr.Width) { 'is outside the tree' } else { 'is inside the tree' }) + ')')
    [W32]::Click([int](($visL + $visR) / 2), [int]$cy)
    Start-Sleep -Milliseconds 600
    # Two channels that say whether the press reached the row even when it cannot select it: the
    # tree takes keyboard focus on a press, and the row's own UIA state names both selectability
    # and focus. Printed because a panel whose rows are enabled-but-not-selectable would otherwise
    # read exactly like a click that missed. The row element is looked up again after the press:
    # opening a row rebuilt this tree's accessible objects once already.
    $clickedRow = @(FindType $tree $CT::TreeItem | Where-Object { $_.Current.Name.Trim() -eq $first } |
                    Select-Object -First 1)
    if (-not $clickedRow) { $clickedRow = $rowEls[0] }
    $ri0 = Pat $clickedRow 'SelectionItem'
    Say ('  after the click: tree focused=' + $tree.Current.HasKeyboardFocus +
         ' row focused=' + $clickedRow.Current.HasKeyboardFocus +
         ' row enabled=' + $clickedRow.Current.IsEnabled +
         ' row selection pattern=' + [bool]$ri0 +
         $(if ($ri0) { ' selected=' + $ri0.Current.IsSelected } else { '' }))
    $selNames1 = @()
    if ($treeSel) {
      try { $selNames1 = @($treeSel.Current.GetSelection() | ForEach-Object { $_.Current.Name }) } catch { }
    }
    Say ('  selection after clicking the first row: [' + ($selNames1 -join '| |') + ']')
    $selected = $false
    foreach ($n in $selNames1) { if ($n -eq $first) { $selected = $true } }
    if (-not $selected -and -not $treeSel) {
      # The tree may expose selection only on the row itself; ask that as a second channel before
      # calling the press unproven.
      $ri = Pat $clickedRow 'SelectionItem'
      if ($ri) {
        try { if ($ri.Current.IsSelected) { $selected = $true } } catch { }
        Say ('  the row''s own SelectionItemPattern says selected=' + $selected)
      } else {
        Say '  neither the tree nor the row offers a selection pattern to read'
      }
    }
    $selectionDefect = $false
    if ($selected) {
      Say '  the press selected the first row'
    } else {
      $selectionDefect = $true
      Say ('  PRODUCT DEFECT: the press reached the first row (tree focused=' +
           $tree.Current.HasKeyboardFocus + ', row focused=' + $clickedRow.Current.HasKeyboardFocus +
           ') but its selection never changes; the row is enabled and not selectable, so nobody ' +
           '(mouse, keyboard or UIA) can select it')
    }
    $w2 = Window $app.Id
    if (-not $w2) { Bail '  the window went away after the row click' }
    Say ('  the window is still up after the click [' + $w2.Current.Name + ']')

    # ---- the picture ------------------------------------------------------------------------
    $tabs = @(FindType (Window $app.Id) $CT::TabItem)
    $sideTabs = @($tabs | Where-Object { (SidebarTabNames) -contains $_.Current.Name })
    # In English the four labels are wider than the sidebar, so the strip scrolls: once the
    # signatures page is current the first tabs sit left of the panel (measured -77 and -6 window
    # px). Those are not on the reader's screen, so they are not framed; they are printed instead,
    # because a strip that has to scroll is itself a reading about the layout.
    $tr2 = $tree.Current.BoundingRectangle
    $kept = @()
    foreach ($t in $sideTabs) {
      $r = $null
      try { $r = $t.Current.BoundingRectangle } catch { }
      if ($r -and $r.Width -gt 0 -and $r.X -ge ($tr2.X - 4) -and ($r.X + $r.Width) -le ($tr2.X + $tr2.Width + 4)) {
        $kept += $t
      } else {
        Say ('  tab |' + $t.Current.Name + '| is scrolled out of the sidebar strip' +
             $(if ($r) { ' (x=' + [int]$r.X + ', panel x=' + [int]$tr2.X + ')' } else { '' }))
      }
    }
    $sideTabs = $kept
    $wrong = ($Control -and $Sample -eq 'broken-blob')
    if ($wrong) {
      Say '  CONTROL: the crop will be anchored to the client rectangle (the sec.89 mistake)'
    }
    Save-WidgetPicture $hwnd ($Sample + '-' + $readLang) @($sideTabs + @($tree)) $wrong
    Say ('  the screen holds: ' + $rowText.Count + ' signature row(s), verdict [' + $wantVerdict +
         '], ' + $wantInc + ' revision line(s)' + $(if ($Control) { '  (CONTROL)' } else { '' }))
    if ($selectionDefect) {
      Bail ('  the screen shows the expected rows and verdicts, but clicking a signature row does ' +
            'not change the selection: the row is enabled-not-selectable, so Qt refuses to select ' +
            'it (PRODUCT DEFECT, reported rather than fixed)')
    }
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
Kill-ProbeApp
if (-not $guardHeld) {
  Say 'GUARD FAILED: a save mode wrote the file it was handed, not the copy it made'
  exit 1
}
Say 'DONE'
