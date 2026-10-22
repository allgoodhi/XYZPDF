// Where a bookmark's destination lands on the page as the reader is looking at it.
//
// The destination numbers in a PDF are in default user space: origin bottom-left, y up, and they do
// not know which way the reader has turned the page. The layout's rectangles are in the opposite
// space: origin top-left, y down, with the reader's rotation already applied by PageMetrics. This
// is the one conversion between the two for a bookmark jump -- one function, so the sidebar's click
// and the test that checks where it landed cannot each do the arithmetic their own way.
#pragma once

#include "outline.h"

#include <QSizeF>

// How far down the drawn page a click on this bookmark should put the top of the viewport, in
// points of the page as drawn (the page's own /Rotate is already folded into `pagePoints`, the
// reader's rotation is `rotationDegrees`). Zero means the page's top edge.
//
// Only the vertical place is answered, which is what a bookmark jump needs and what /FitH names;
// a horizontal scroll position would undo where a zoomed-in reader had panned to, the same rule
// goToPage() follows.
//
// Kinds that name no vertical place answer zero: /Fit, /FitB and a page-only destination have
// none at all; /FitV and /FitBV name only an x; and a null where the kind wants a y means "leave
// the position alone", which is not a target to jump to. An /XYZ whose x is null is answered as
// page top under a quarter turn, because the turn makes that unknown x the vertical coordinate.
//
// Not measured: a page whose file carries its own /Rotate. `pagePoints` is the size Qt reports for
// it (that rotation already folded in), and whether this file's destination numbers are written in
// the pre-rotation space is a question the test's pages -- all /Rotate 0 -- cannot answer.
double bookmarkTopOffset(const BookmarkDest &dest, QSizeF pagePoints, int rotationDegrees);
