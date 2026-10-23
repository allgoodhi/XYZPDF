// The one place the program names a concrete Document.
//
// Document is the seam every reader, renderer, shell handler and test talks to;
// keeping the implementation out of their sources means replacing it is a change
// to this file rather than to a search-and-replace across the program. The
// factory is deliberately a free function rather than a registry: there is one
// backend in a build, chosen when the program is built, and an interface that
// could hand out two of them would invite a program that has to work with both.
//
// The choice arrived in F7-B as a build switch and stopped being one in F7-E:
// there is one backend, it is PDFium, and this file is still the only place a
// concrete Document is named -- which is what let the view, the two shell
// handlers and the print and export paths change engines without changing.
#pragma once

#include <memory>

class Document;

std::unique_ptr<Document> createDocument();
