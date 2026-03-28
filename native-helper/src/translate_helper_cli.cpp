#include "pdf_text_extractor.h"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: translate-pdf-cli <pdf-path> <zero-based-page>\n";
        return 2;
    }

    int page = std::atoi(argv[2]);
    translate_helper::ExtractResult result =
        translate_helper::extract_pdf_page_text_from_path(argv[1], page);
    if (!result.ok) {
        std::cerr << "error: " << result.error << "\n";
        return 1;
    }

    std::cout << result.text;
    return 0;
}
