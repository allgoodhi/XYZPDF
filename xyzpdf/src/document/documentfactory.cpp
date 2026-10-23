#include "documentfactory.h"

#include "document.h"

#if defined(XYZPDF_DOCUMENT_BACKEND_PDFIUM)
#include "pdfiumbackend.h"
#else
#include "qtpdfbackend.h"
#endif

std::unique_ptr<Document> createDocument()
{
#if defined(XYZPDF_DOCUMENT_BACKEND_PDFIUM)
    return std::make_unique<PdfiumBackend>();
#else
    return std::make_unique<QtPdfBackend>();
#endif
}
