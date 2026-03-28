#ifndef TRANSLATE_HELPER_PDF_TEXT_EXTRACTOR_H
#define TRANSLATE_HELPER_PDF_TEXT_EXTRACTOR_H

#include <string>

namespace translate_helper {

struct ExtractResult {
    bool ok = false;
    std::string text;
    std::string error;
};

ExtractResult extract_pdf_page_text_from_path(const std::string &pdf_path, int page_index);

}  // namespace translate_helper

#endif
