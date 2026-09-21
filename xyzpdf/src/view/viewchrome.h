// What the window looks like while the reader is not looking at the window.
//
// ---- why this is a separate file of plain functions ----
//
// Fullscreen and presentation mode are almost entirely a state machine over
// which parts of a window are visible, and that part is the part a test can
// actually reach: showing a window full-screen, moving a real pointer into a
// reveal band and watching a toolbar slide out is not reproducible offscreen.
// So the decisions live here as functions of their inputs, and MainWindow is
// left with only the applying -- which is the half that has to be checked by
// eye, and is smaller for it.
#pragma once

#include <Qt>

namespace ViewChrome {

// How much of the window's own furniture is on screen.
enum class Mode {
    // Menus, toolbars, status bar, sidebar, tab strip: all of it.
    Normal,
    // The page as large as the screen allows, with the toolbars brought back by
    // pushing the pointer to the top edge. The tab strip stays: a reader who
    // goes fullscreen to see one page better still wants to leave it for
    // another without leaving fullscreen.
    FullScreen,
    // Fullscreen, and further: one page at a time, fitted, turned by a click or
    // an arrow key, with nothing of the application left on screen. The tab
    // strip goes too, because this is the mode where other people are looking
    // at the window.
    Presentation
};

// Which furniture each mode hides. Spelled out one function at a time rather
// than as a mask, so that adding a mode is a question that has to be answered
// for every part rather than one number that quietly covers them all.
inline bool hidesMenu(Mode mode)
{
    return mode != Mode::Normal;
}

inline bool hidesStatus(Mode mode)
{
    return mode != Mode::Normal;
}

inline bool hidesSidebar(Mode mode)
{
    return mode != Mode::Normal;
}

inline bool hidesTabStrip(Mode mode)
{
    return mode == Mode::Presentation;
}

// The toolbars are hidden in both, but only in the sense of "out of the way":
// in fullscreen they come back under the pointer. In presentation they do not,
// because a toolbar appearing over a projected slide covers the slide.
inline bool toolbarsCanReveal(Mode mode)
{
    return mode == Mode::FullScreen;
}

inline bool hidesToolBars(Mode mode)
{
    return mode != Mode::Normal;
}

// How far from the top edge the pointer has to be to bring the toolbars back,
// in the window's own coordinates. Deliberately a band rather than "the toolbar
// is under the cursor": the band is what the reader learns to aim at, and it has
// to be reachable without the pointer leaving the window.
constexpr int kRevealBandPixels = 6;

inline bool inRevealBand(int yInWindow)
{
    return yInWindow >= 0 && yInWindow < kRevealBandPixels;
}

// A key press in presentation mode, as a page move: +1 forward, -1 back, 0 for
// "not a page turn, let the window handle it".
//
// Space goes forward and Shift+Space back, which is what the page view already
// does for scrolling and what every reader does -- a presentation that reversed
// it would be the one place where Space means something else.
inline int presentationStep(int key, Qt::KeyboardModifiers modifiers)
{
    switch (key) {
    case Qt::Key_Space:
        return modifiers.testFlag(Qt::ShiftModifier) ? -1 : 1;
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_PageDown:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return 1;
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace:
        return -1;
    default:
        return 0;
    }
}

// Where a click in presentation mode turns the page. The left third goes back:
// a presenter who has just shown a slide they wanted to say more about needs the
// way back to be under the same hand as the way forward, and does not want it to
// be the keyboard they are already talking over.
inline int presentationClickStep(int xInViewport, int viewportWidth)
{
    return xInViewport < viewportWidth / 3 ? -1 : 1;
}

} // namespace ViewChrome
