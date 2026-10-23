#include "documentfactory.h"

#include "document.h"
#include "pdfiumbackend.h"

std::unique_ptr<Document> createDocument()
{
    return std::make_unique<PdfiumBackend>();
}
