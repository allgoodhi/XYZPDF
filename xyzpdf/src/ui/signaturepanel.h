// The document's digital signatures, as the fourth tab of the reading sidebar.
//
// One row per signature, and the row says the four-word answer first: 有效 / 内容被改过 /
// 证书不受信任或已过期 / 无法验证. Under it, expandable, the detail lines SignatureVerify
// builds -- who signed, when and whose word that time is, what was covered, whether anything was
// appended after it, and whether the revocation question was taken to the network.
//
// ---- why a tree and not a list ----
//
// The verdict is one line and the evidence is a dozen; a reader who wants the second has to be
// able to open it without a dialog per signature, and one who does not has to be able to ignore
// it. A QTreeWidget with the detail lines as children is the smallest thing that does both, and
// it is what the bookmarks panel already is, so the sidebar does not grow a fourth shape.
//
// ---- what it is not ----
//
// Not the checker: src/document/signatureverify.cpp decides, this shows. Not the jump either --
// a row pressed is a page number, and where on the page is not something a signature says in a
// form this program reads (the widget's /Rect is in the file, but a signature's page is what a
// reader wants when they press one).
//
// The rows are also what the tests read. `rowText()` and `pageOfRow()` exist so that a check of
// "the panel shows one row per signature and pressing it goes to the right page" does not have to
// paint anything.
#pragma once

#include "../document/signatureverify.h"

#include <QTreeWidget>

class SignaturePanel : public QTreeWidget
{
    Q_OBJECT

public:
    explicit SignaturePanel(QWidget *parent = nullptr);

    // Rebuild the tree from a finished verification. Empty clears it, and an empty panel is the
    // reason the tab hides this page for a document with no signatures.
    void setSignatures(const QVector<SignatureVerify::Signature> &signatures);

    bool hasSignatures() const;
    int signatureRowCount() const;
    // The row's own text, for a test that would otherwise be reading pixels: the verdict, the
    // signer's name and the time on one line.
    QString rowText(int row) const;
    // Which page that row jumps to, or -1 when the signature could not be placed on a page.
    int pageOfRow(int row) const;
    // The verdict of a row, as the panel shows it.
    QString verdictOfRow(int row) const;
    // Whether the reader has the detail lines open, for the test that presses a row's expander.
    bool rowIsExpanded(int row) const;
    void selectRow(int row);
    // Does what a click on that row does, including the jump. A test that has to synthesise a
    // mouse press inside a tree widget is a test of Qt's event plumbing rather than of this
    // panel's arrangement, and the keyboard path (itemActivated) already goes through here.
    void pressRow(int row);

Q_SIGNALS:
    // The reader pressed a signature row. Zero-based page; never sent for a row the panel could
    // not place, because sending -1 would put the reader on the last page instead.
    void jumpToPage(int page);

private:
    void rowPressed();

    // The page number travels with the top-level row rather than being looked up by index, so
    // that a detail child being pressed cannot be read as a signature being pressed.
    static constexpr int PageRole = Qt::UserRole + 1;
};
