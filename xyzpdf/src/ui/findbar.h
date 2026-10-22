// The bar a search is typed into.
//
// A widget rather than a dialog, and inside the tab rather than in the window:
// a search belongs to the document it is looking in, and a reader moving
// between two open documents should find each one's own search where they left
// it. It says what is being searched and how many hits there are, and it does
// not pretend to know anything else -- the searching itself belongs to the tab,
// which is what has the document.
#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QToolButton;

class FindBar : public QWidget
{
    Q_OBJECT

public:
    explicit FindBar(QWidget *parent = nullptr);

    // Puts the caret in the field and selects what is there, which is what
    // pressing Ctrl+F twice should do: type over the last search, or press
    // Enter to go to the next hit.
    void focusField();
    QString text() const;
    bool caseSensitive() const;
    bool wholeWord() const;
    // Shows a needle the user did not type here -- the window's toolbar box is
    // the other place a search can be typed, and this is how the two stay one
    // search rather than two. Deliberately not a signal: nothing is being asked
    // for, so no search is started by it.
    void setText(const QString &text);

    // How many hits, and which one is being looked at. -1 for "none" and for
    // "still counting", which are told apart by `searching`.
    void setResultCount(int current, int total, bool searching);

Q_SIGNALS:
    // The needle changed (typing), or one of the two boxes was ticked.
    void searchRequested(const QString &needle, bool caseSensitive, bool wholeWord);
    // Next (+1) or previous (-1). Wraps, which is the tab's business.
    void stepRequested(int delta);
    // "List them all" was pressed: the reader wants the hits as rows, wherever
    // the sidebar is now. Opening it is the tab's business, not the bar's.
    void listAllRequested();
    // Escape, or the close button.
    void closed();

protected:
    // Enter goes forward and Shift+Enter back, and Escape closes the bar --
    // handled here rather than in the tab so that the keys work wherever the
    // focus is inside the bar.
    void keyPressEvent(QKeyEvent *event) override;

private:
    QLineEdit *field = nullptr;
    QCheckBox *caseBox = nullptr;
    QCheckBox *wholeBox = nullptr;
    QLabel *count = nullptr;
};
