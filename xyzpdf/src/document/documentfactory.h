// The one place the program names a concrete Document.
//
// Document is the seam every reader, renderer, shell handler and test talks to;
// keeping the implementation out of their sources means replacing it is a change
// to this file rather than to a search-and-replace across the program. The
// factory is deliberately a free function rather than a registry: there is one
// backend in a build, chosen when the program is built, and an interface that
// could hand out two of them would invite a program that has to work with both.
//
// The choice arrived in F7-B and lives in the .cpp: one of the two headers is
// included, the other is not compiled at all, and the program carries the engine
// it was built with and no second one. F7-E removes Qt PDF and leaves one branch.
#pragma once

#include <memory>

class Document;

std::unique_ptr<Document> createDocument();
