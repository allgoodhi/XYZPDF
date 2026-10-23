// The one place the program names a concrete Document.
//
// Document is the seam every reader, renderer, shell handler and test talks to;
// keeping the implementation out of their sources means replacing it is a change
// to this file rather than to a search-and-replace across the program. The
// factory is deliberately a free function rather than a registry: there is one
// backend in a build, chosen when the program is built, and an interface that
// could hand out two of them would invite a program that has to work with both.
//
// F7-B adds the build-time choice here (docs/pdfium-plan.md's
// XYZPDF_DOCUMENT_BACKEND); F7-E removes Qt PDF and leaves one branch.
#pragma once
#pragma once

#include <memory>

class Document;

std::unique_ptr<Document> createDocument();
