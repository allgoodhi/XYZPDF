#include "documentfactory.h"

#include "qtpdfbackend.h"

std::unique_ptr<Document> createDocument()
{
    return std::make_unique<QtPdfBackend>();
}
