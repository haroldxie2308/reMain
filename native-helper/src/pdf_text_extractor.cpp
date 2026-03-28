#include "pdf_text_extractor.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <zlib.h>

namespace translate_helper {

namespace {

struct PdfRef {
    int object = 0;
    int generation = 0;

    bool valid() const { return object > 0; }
};

struct PdfValue {
    enum Type {
        kNull,
        kBool,
        kNumber,
        kName,
        kLiteralString,
        kHexString,
        kArray,
        kDict,
        kRef
    } type = kNull;

    bool bool_value = false;
    double number_value = 0.0;
    std::string string_value;
    std::vector<PdfValue> array_value;
    std::unordered_map<std::string, PdfValue> dict_value;
    PdfRef ref_value;

    static PdfValue make_null() { return PdfValue(); }

    static PdfValue make_bool(bool value) {
        PdfValue out;
        out.type = kBool;
        out.bool_value = value;
        return out;
    }

    static PdfValue make_number(double value) {
        PdfValue out;
        out.type = kNumber;
        out.number_value = value;
        return out;
    }

    static PdfValue make_name(std::string value) {
        PdfValue out;
        out.type = kName;
        out.string_value = std::move(value);
        return out;
    }

    static PdfValue make_literal(std::string value) {
        PdfValue out;
        out.type = kLiteralString;
        out.string_value = std::move(value);
        return out;
    }

    static PdfValue make_hex(std::string value) {
        PdfValue out;
        out.type = kHexString;
        out.string_value = std::move(value);
        return out;
    }

    static PdfValue make_array(std::vector<PdfValue> value) {
        PdfValue out;
        out.type = kArray;
        out.array_value = std::move(value);
        return out;
    }

    static PdfValue make_dict(std::unordered_map<std::string, PdfValue> value) {
        PdfValue out;
        out.type = kDict;
        out.dict_value = std::move(value);
        return out;
    }

    static PdfValue make_ref(int object, int generation) {
        PdfValue out;
        out.type = kRef;
        out.ref_value = PdfRef{object, generation};
        return out;
    }
};

struct PdfObject {
    bool loaded = false;
    PdfValue value;
    bool has_stream = false;
    std::string stream_bytes;
};

struct FontDecoder {
    int code_bytes = 1;
    std::unordered_map<unsigned int, std::string> glyph_to_unicode;
};

struct PageInfo {
    PdfRef ref;
    PdfValue resources;
};

bool is_space(unsigned char ch) {
    return ch == 0 || ch == 9 || ch == 10 || ch == 12 || ch == 13 || ch == 32;
}

void skip_ws_and_comments(const std::string &src, size_t *pos) {
    while (*pos < src.size()) {
        unsigned char ch = static_cast<unsigned char>(src[*pos]);
        if (is_space(ch)) {
            ++(*pos);
            continue;
        }
        if (ch == '%') {
            while (*pos < src.size() && src[*pos] != '\n' && src[*pos] != '\r') {
                ++(*pos);
            }
            continue;
        }
        break;
    }
}

bool starts_with_at(const std::string &src, size_t pos, const char *needle) {
    size_t needle_len = std::strlen(needle);
    if (pos + needle_len > src.size()) {
        return false;
    }
    return src.compare(pos, needle_len, needle) == 0;
}

bool is_delim(unsigned char ch) {
    return ch == '(' || ch == ')' || ch == '<' || ch == '>' ||
           ch == '[' || ch == ']' || ch == '{' || ch == '}' ||
           ch == '/' || ch == '%';
}

bool parse_int_token(const std::string &src, size_t *pos, int *out) {
    size_t start = *pos;
    if (start >= src.size()) {
        return false;
    }
    if (src[*pos] == '+' || src[*pos] == '-') {
        ++(*pos);
    }
    size_t digits_start = *pos;
    while (*pos < src.size() && std::isdigit(static_cast<unsigned char>(src[*pos]))) {
        ++(*pos);
    }
    if (digits_start == *pos) {
        *pos = start;
        return false;
    }
    std::string token = src.substr(start, *pos - start);
    char *end = nullptr;
    long value = std::strtol(token.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || value < INT_MIN || value > INT_MAX) {
        *pos = start;
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

bool parse_number_token(const std::string &src, size_t *pos, double *out) {
    size_t start = *pos;
    if (start >= src.size()) {
        return false;
    }
    if (src[*pos] == '+' || src[*pos] == '-') {
        ++(*pos);
    }
    bool saw_digit = false;
    while (*pos < src.size() && std::isdigit(static_cast<unsigned char>(src[*pos]))) {
        ++(*pos);
        saw_digit = true;
    }
    if (*pos < src.size() && src[*pos] == '.') {
        ++(*pos);
        while (*pos < src.size() && std::isdigit(static_cast<unsigned char>(src[*pos]))) {
            ++(*pos);
            saw_digit = true;
        }
    }
    if (!saw_digit) {
        *pos = start;
        return false;
    }
    std::string token = src.substr(start, *pos - start);
    char *end = nullptr;
    double value = std::strtod(token.c_str(), &end);
    if (end == nullptr || *end != '\0') {
        *pos = start;
        return false;
    }
    *out = value;
    return true;
}

std::string read_name(const std::string &src, size_t *pos) {
    std::string out;
    if (*pos >= src.size() || src[*pos] != '/') {
        return out;
    }
    ++(*pos);
    while (*pos < src.size()) {
        unsigned char ch = static_cast<unsigned char>(src[*pos]);
        if (is_space(ch) || is_delim(ch)) {
            break;
        }
        if (ch == '#' && *pos + 2 < src.size() &&
            std::isxdigit(static_cast<unsigned char>(src[*pos + 1])) &&
            std::isxdigit(static_cast<unsigned char>(src[*pos + 2]))) {
            std::string hex = src.substr(*pos + 1, 2);
            out.push_back(static_cast<char>(std::strtol(hex.c_str(), nullptr, 16)));
            *pos += 3;
            continue;
        }
        out.push_back(static_cast<char>(ch));
        ++(*pos);
    }
    return out;
}

std::string read_literal_string(const std::string &src, size_t *pos) {
    std::string out;
    if (*pos >= src.size() || src[*pos] != '(') {
        return out;
    }
    ++(*pos);
    int depth = 1;
    while (*pos < src.size() && depth > 0) {
        char ch = src[*pos];
        ++(*pos);
        if (ch == '\\') {
            if (*pos >= src.size()) {
                break;
            }
            char next = src[*pos];
            ++(*pos);
            switch (next) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case '(': out.push_back('('); break;
                case ')': out.push_back(')'); break;
                case '\\': out.push_back('\\'); break;
                case '\n': break;
                case '\r':
                    if (*pos < src.size() && src[*pos] == '\n') {
                        ++(*pos);
                    }
                    break;
                default:
                    if (next >= '0' && next <= '7') {
                        std::string octal(1, next);
                        for (int i = 0; i < 2 && *pos < src.size(); ++i) {
                            char oct = src[*pos];
                            if (oct < '0' || oct > '7') {
                                break;
                            }
                            octal.push_back(oct);
                            ++(*pos);
                        }
                        out.push_back(static_cast<char>(std::strtol(octal.c_str(), nullptr, 8)));
                    } else {
                        out.push_back(next);
                    }
                    break;
            }
            continue;
        }
        if (ch == '(') {
            ++depth;
            out.push_back(ch);
            continue;
        }
        if (ch == ')') {
            --depth;
            if (depth > 0) {
                out.push_back(ch);
            }
            continue;
        }
        out.push_back(ch);
    }
    return out;
}

std::string read_hex_string(const std::string &src, size_t *pos) {
    std::string out;
    if (*pos >= src.size() || src[*pos] != '<' || starts_with_at(src, *pos, "<<")) {
        return out;
    }
    ++(*pos);
    while (*pos < src.size() && src[*pos] != '>') {
        unsigned char ch = static_cast<unsigned char>(src[*pos]);
        if (!is_space(ch)) {
            out.push_back(static_cast<char>(ch));
        }
        ++(*pos);
    }
    if (*pos < src.size() && src[*pos] == '>') {
        ++(*pos);
    }
    if ((out.size() % 2) != 0) {
        out.push_back('0');
    }
    return out;
}

PdfValue parse_pdf_value(const std::string &src, size_t *pos);

PdfValue parse_array(const std::string &src, size_t *pos) {
    std::vector<PdfValue> out;
    ++(*pos);
    while (*pos < src.size()) {
        skip_ws_and_comments(src, pos);
        if (*pos >= src.size()) {
            break;
        }
        if (src[*pos] == ']') {
            ++(*pos);
            break;
        }
        out.push_back(parse_pdf_value(src, pos));
    }
    return PdfValue::make_array(std::move(out));
}

PdfValue parse_dict(const std::string &src, size_t *pos) {
    std::unordered_map<std::string, PdfValue> out;
    *pos += 2;
    while (*pos < src.size()) {
        skip_ws_and_comments(src, pos);
        if (*pos + 1 < src.size() && src[*pos] == '>' && src[*pos + 1] == '>') {
            *pos += 2;
            break;
        }
        if (*pos >= src.size() || src[*pos] != '/') {
            break;
        }
        std::string key = read_name(src, pos);
        skip_ws_and_comments(src, pos);
        out[key] = parse_pdf_value(src, pos);
    }
    return PdfValue::make_dict(std::move(out));
}

PdfValue parse_number_or_ref(const std::string &src, size_t *pos) {
    size_t start = *pos;
    double number_value = 0.0;
    if (!parse_number_token(src, pos, &number_value)) {
        return PdfValue::make_null();
    }

    bool looks_int = std::floor(number_value) == number_value;
    if (looks_int) {
        size_t after_first = *pos;
        skip_ws_and_comments(src, pos);
        int generation = 0;
        size_t second_start = *pos;
        if (parse_int_token(src, pos, &generation)) {
            size_t after_second = *pos;
            skip_ws_and_comments(src, pos);
            if (*pos < src.size() && src[*pos] == 'R') {
                ++(*pos);
                return PdfValue::make_ref(static_cast<int>(number_value), generation);
            }
            *pos = after_second;
            *pos = second_start;
        } else {
            *pos = after_first;
        }
    }

    *pos = start;
    parse_number_token(src, pos, &number_value);
    return PdfValue::make_number(number_value);
}

PdfValue parse_pdf_value(const std::string &src, size_t *pos) {
    skip_ws_and_comments(src, pos);
    if (*pos >= src.size()) {
        return PdfValue::make_null();
    }

    char ch = src[*pos];
    if (ch == '<' && *pos + 1 < src.size() && src[*pos + 1] == '<') {
        return parse_dict(src, pos);
    }
    if (ch == '[') {
        return parse_array(src, pos);
    }
    if (ch == '/') {
        return PdfValue::make_name(read_name(src, pos));
    }
    if (ch == '(') {
        return PdfValue::make_literal(read_literal_string(src, pos));
    }
    if (ch == '<') {
        return PdfValue::make_hex(read_hex_string(src, pos));
    }
    if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '+' || ch == '-' || ch == '.') {
        return parse_number_or_ref(src, pos);
    }
    if (starts_with_at(src, *pos, "true")) {
        *pos += 4;
        return PdfValue::make_bool(true);
    }
    if (starts_with_at(src, *pos, "false")) {
        *pos += 5;
        return PdfValue::make_bool(false);
    }
    if (starts_with_at(src, *pos, "null")) {
        *pos += 4;
        return PdfValue::make_null();
    }

    size_t start = *pos;
    while (*pos < src.size()) {
        unsigned char cur = static_cast<unsigned char>(src[*pos]);
        if (is_space(cur) || is_delim(cur)) {
            break;
        }
        ++(*pos);
    }
    return PdfValue::make_name(src.substr(start, *pos - start));
}

std::string read_file(const std::string &path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open PDF: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string trim_copy(const std::string &value) {
    size_t start = 0;
    size_t end = value.size();
    while (start < end && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string inflate_flate(const std::string &data) {
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());

    if (inflateInit(&stream) != Z_OK) {
        throw std::runtime_error("inflateInit failed");
    }

    std::string out;
    int ret = Z_OK;
    char buffer[32768];
    while (ret == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef *>(buffer);
        stream.avail_out = sizeof(buffer);
        ret = inflate(&stream, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&stream);
            throw std::runtime_error("inflate failed");
        }
        out.append(buffer, sizeof(buffer) - stream.avail_out);
    }

    inflateEnd(&stream);
    return out;
}

std::vector<unsigned char> hex_to_bytes(const std::string &hex) {
    std::vector<unsigned char> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        char buf[3];
        buf[0] = hex[i];
        buf[1] = hex[i + 1];
        buf[2] = '\0';
        out.push_back(static_cast<unsigned char>(std::strtoul(buf, nullptr, 16)));
    }
    return out;
}

std::string utf16be_bytes_to_utf8(const std::vector<unsigned char> &bytes) {
    std::string out;
    size_t i = 0;
    while (i + 1 < bytes.size()) {
        unsigned int codepoint = (static_cast<unsigned int>(bytes[i]) << 8) |
                                 static_cast<unsigned int>(bytes[i + 1]);
        i += 2;

        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && i + 1 < bytes.size()) {
            unsigned int low = (static_cast<unsigned int>(bytes[i]) << 8) |
                               static_cast<unsigned int>(bytes[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                i += 2;
                codepoint = 0x10000 + (((codepoint - 0xD800) << 10) | (low - 0xDC00));
            }
        }

        if (codepoint <= 0x7F) {
            out.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }
    return out;
}

std::string hex_utf16be_to_utf8(const std::string &hex) {
    return utf16be_bytes_to_utf8(hex_to_bytes(hex));
}

unsigned int hex_to_uint(const std::string &hex) {
    return static_cast<unsigned int>(std::strtoul(hex.c_str(), nullptr, 16));
}

class PdfDocument {
public:
    explicit PdfDocument(std::string bytes) : bytes_(std::move(bytes)) {}

    void load() {
        parse_xref();
        parse_trailer();
    }

    std::string extract_page_text(int page_index) {
        std::vector<PageInfo> pages = collect_pages();
        if (page_index < 0 || page_index >= static_cast<int>(pages.size())) {
            throw std::runtime_error("page index out of range");
        }
        const PageInfo &page = pages[page_index];
        std::unordered_map<std::string, FontDecoder> fonts = build_font_map(page.resources);
        std::string contents = decode_page_contents(page.ref);
        std::string parsed = parse_content_stream(contents, fonts);
        return normalize_text(parsed);
    }

private:
    std::string bytes_;
    std::unordered_map<int, size_t> xref_offsets_;
    PdfValue trailer_;
    std::unordered_map<int, PdfObject> object_cache_;

    void parse_xref() {
        size_t startxref_pos = bytes_.rfind("startxref");
        if (startxref_pos == std::string::npos) {
            throw std::runtime_error("PDF startxref not found");
        }
        size_t pos = startxref_pos + std::strlen("startxref");
        skip_ws_and_comments(bytes_, &pos);
        int xref_offset = 0;
        if (!parse_int_token(bytes_, &pos, &xref_offset)) {
            throw std::runtime_error("PDF xref offset missing");
        }

        pos = static_cast<size_t>(xref_offset);
        skip_ws_and_comments(bytes_, &pos);
        if (!starts_with_at(bytes_, pos, "xref")) {
            throw std::runtime_error("xref streams are not supported");
        }
        pos += 4;

        while (pos < bytes_.size()) {
            skip_ws_and_comments(bytes_, &pos);
            if (starts_with_at(bytes_, pos, "trailer")) {
                break;
            }
            int start_object = 0;
            int count = 0;
            if (!parse_int_token(bytes_, &pos, &start_object)) {
                throw std::runtime_error("invalid xref subsection start");
            }
            skip_ws_and_comments(bytes_, &pos);
            if (!parse_int_token(bytes_, &pos, &count)) {
                throw std::runtime_error("invalid xref subsection count");
            }
            for (int i = 0; i < count; ++i) {
                skip_ws_and_comments(bytes_, &pos);
                size_t line_start = pos;
                while (pos < bytes_.size() && bytes_[pos] != '\n' && bytes_[pos] != '\r') {
                    ++pos;
                }
                std::string line = trim_copy(bytes_.substr(line_start, pos - line_start));
                if (!line.empty()) {
                    unsigned long offset = 0;
                    unsigned long generation = 0;
                    char in_use = '\0';
                    if (std::sscanf(line.c_str(), "%lu %lu %c", &offset, &generation, &in_use) == 3 &&
                        in_use == 'n') {
                        xref_offsets_[start_object + i] = static_cast<size_t>(offset);
                    }
                }
            }
        }
    }

    void parse_trailer() {
        size_t trailer_pos = bytes_.rfind("trailer");
        if (trailer_pos == std::string::npos) {
            throw std::runtime_error("PDF trailer not found");
        }
        size_t pos = trailer_pos + std::strlen("trailer");
        trailer_ = parse_pdf_value(bytes_, &pos);
        if (trailer_.type != PdfValue::kDict) {
            throw std::runtime_error("PDF trailer is not a dictionary");
        }
    }

    PdfObject &load_object(PdfRef ref) {
        if (!ref.valid()) {
            throw std::runtime_error("invalid indirect reference");
        }

        PdfObject &cached = object_cache_[ref.object];
        if (cached.loaded) {
            return cached;
        }

        std::unordered_map<int, size_t>::const_iterator it = xref_offsets_.find(ref.object);
        if (it == xref_offsets_.end()) {
            throw std::runtime_error("xref entry missing for object " + std::to_string(ref.object));
        }

        size_t pos = it->second;
        skip_ws_and_comments(bytes_, &pos);

        int object_no = 0;
        int generation = 0;
        if (!parse_int_token(bytes_, &pos, &object_no)) {
            throw std::runtime_error("invalid object header at xref offset");
        }
        skip_ws_and_comments(bytes_, &pos);
        if (!parse_int_token(bytes_, &pos, &generation)) {
            throw std::runtime_error("invalid object generation at xref offset");
        }
        skip_ws_and_comments(bytes_, &pos);
        if (!starts_with_at(bytes_, pos, "obj")) {
            throw std::runtime_error("invalid object header at xref offset");
        }
        pos += 3;

        size_t value_pos = pos;
        PdfValue value = parse_pdf_value(bytes_, &value_pos);
        cached.value = value;

        size_t after_value = value_pos;
        skip_ws_and_comments(bytes_, &after_value);
        if (starts_with_at(bytes_, after_value, "stream")) {
            size_t stream_start = after_value + std::strlen("stream");
            if (stream_start < bytes_.size() && bytes_[stream_start] == '\r') {
                ++stream_start;
                if (stream_start < bytes_.size() && bytes_[stream_start] == '\n') {
                    ++stream_start;
                }
            } else if (stream_start < bytes_.size() && bytes_[stream_start] == '\n') {
                ++stream_start;
            }

            size_t stream_end = bytes_.find("endstream", stream_start);
            if (stream_end == std::string::npos) {
                throw std::runtime_error("stream without endstream");
            }
            while (stream_end > stream_start &&
                   (bytes_[stream_end - 1] == '\n' || bytes_[stream_end - 1] == '\r')) {
                --stream_end;
            }
            cached.has_stream = true;
            cached.stream_bytes = bytes_.substr(stream_start, stream_end - stream_start);
        }

        cached.loaded = true;
        return cached;
    }

    const PdfValue *resolve_value(const PdfValue &value) {
        if (value.type == PdfValue::kRef) {
            return &load_object(value.ref_value).value;
        }
        return &value;
    }

    const std::unordered_map<std::string, PdfValue> *resolve_dict(const PdfValue &value) {
        const PdfValue *resolved = resolve_value(value);
        if (resolved == nullptr || resolved->type != PdfValue::kDict) {
            return nullptr;
        }
        return &resolved->dict_value;
    }

    std::vector<PdfRef> value_to_ref_array(const PdfValue &value) {
        std::vector<PdfRef> out;
        const PdfValue *resolved = resolve_value(value);
        if (resolved == nullptr) {
            return out;
        }
        if (resolved->type == PdfValue::kRef) {
            out.push_back(resolved->ref_value);
            return out;
        }
        if (resolved->type != PdfValue::kArray) {
            if (value.type == PdfValue::kRef) {
                out.push_back(value.ref_value);
            }
            return out;
        }
        for (size_t i = 0; i < resolved->array_value.size(); ++i) {
            const PdfValue &entry = resolved->array_value[i];
            if (entry.type == PdfValue::kRef) {
                out.push_back(entry.ref_value);
            }
        }
        return out;
    }

    PdfRef root_pages_ref() {
        std::unordered_map<std::string, PdfValue>::const_iterator root_it = trailer_.dict_value.find("Root");
        if (root_it == trailer_.dict_value.end() || root_it->second.type != PdfValue::kRef) {
            throw std::runtime_error("PDF trailer root missing");
        }
        PdfObject &catalog = load_object(root_it->second.ref_value);
        if (catalog.value.type != PdfValue::kDict) {
            throw std::runtime_error("catalog is not a dictionary");
        }
        std::unordered_map<std::string, PdfValue>::const_iterator pages_it = catalog.value.dict_value.find("Pages");
        if (pages_it == catalog.value.dict_value.end() || pages_it->second.type != PdfValue::kRef) {
            throw std::runtime_error("catalog Pages reference missing");
        }
        return pages_it->second.ref_value;
    }

    void collect_pages_recursive(PdfRef ref, const PdfValue *inherited_resources, std::vector<PageInfo> *pages) {
        PdfObject &object = load_object(ref);
        if (object.value.type != PdfValue::kDict) {
            return;
        }

        const std::unordered_map<std::string, PdfValue> &dict = object.value.dict_value;
        const PdfValue *resources = inherited_resources;
        std::unordered_map<std::string, PdfValue>::const_iterator resources_it = dict.find("Resources");
        if (resources_it != dict.end()) {
            resources = &resources_it->second;
        }

        std::unordered_map<std::string, PdfValue>::const_iterator type_it = dict.find("Type");
        if (type_it != dict.end() && type_it->second.type == PdfValue::kName &&
            type_it->second.string_value == "Page") {
            PageInfo page;
            page.ref = ref;
            if (resources != nullptr) {
                page.resources = *resources;
            }
            pages->push_back(page);
            return;
        }

        std::unordered_map<std::string, PdfValue>::const_iterator kids_it = dict.find("Kids");
        if (kids_it == dict.end()) {
            return;
        }
        std::vector<PdfRef> kids = value_to_ref_array(kids_it->second);
        for (size_t i = 0; i < kids.size(); ++i) {
            collect_pages_recursive(kids[i], resources, pages);
        }
    }

    std::vector<PageInfo> collect_pages() {
        std::vector<PageInfo> pages;
        PdfRef pages_root = root_pages_ref();
        collect_pages_recursive(pages_root, nullptr, &pages);
        if (pages.empty()) {
            throw std::runtime_error("no pages found in PDF");
        }
        return pages;
    }

    std::string decode_stream_bytes(const PdfObject &object) {
        if (!object.has_stream) {
            return std::string();
        }
        if (object.value.type != PdfValue::kDict) {
            return object.stream_bytes;
        }
        std::unordered_map<std::string, PdfValue>::const_iterator filter_it = object.value.dict_value.find("Filter");
        if (filter_it == object.value.dict_value.end()) {
            return object.stream_bytes;
        }

        const PdfValue *filter_value = resolve_value(filter_it->second);
        if (filter_value == nullptr) {
            return object.stream_bytes;
        }

        bool flate = false;
        if (filter_value->type == PdfValue::kName) {
            flate = filter_value->string_value == "FlateDecode";
        } else if (filter_value->type == PdfValue::kArray) {
            for (size_t i = 0; i < filter_value->array_value.size(); ++i) {
                const PdfValue &entry = filter_value->array_value[i];
                if (entry.type == PdfValue::kName && entry.string_value == "FlateDecode") {
                    flate = true;
                }
            }
        }
        if (!flate) {
            return object.stream_bytes;
        }
        return inflate_flate(object.stream_bytes);
    }

    std::string decode_page_contents(PdfRef page_ref) {
        PdfObject &page_object = load_object(page_ref);
        if (page_object.value.type != PdfValue::kDict) {
            throw std::runtime_error("page object is not a dictionary");
        }

        std::unordered_map<std::string, PdfValue>::const_iterator contents_it =
            page_object.value.dict_value.find("Contents");
        if (contents_it == page_object.value.dict_value.end()) {
            return std::string();
        }

        std::vector<PdfRef> refs = value_to_ref_array(contents_it->second);
        std::string out;
        for (size_t i = 0; i < refs.size(); ++i) {
            PdfObject &stream_object = load_object(refs[i]);
            out += decode_stream_bytes(stream_object);
            out.push_back('\n');
        }
        return out;
    }

    FontDecoder parse_to_unicode_cmap(const std::string &cmap_text) {
        FontDecoder decoder;
        std::vector<std::string> tokens;
        std::string current;
        size_t pos = 0;
        while (pos < cmap_text.size()) {
            unsigned char ch = static_cast<unsigned char>(cmap_text[pos]);
            if (std::isspace(ch)) {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
                ++pos;
                continue;
            }
            if (ch == '%') {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
                while (pos < cmap_text.size() && cmap_text[pos] != '\n' && cmap_text[pos] != '\r') {
                    ++pos;
                }
                continue;
            }
            if (ch == '[' || ch == ']') {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
                tokens.push_back(std::string(1, static_cast<char>(ch)));
                ++pos;
                continue;
            }
            current.push_back(static_cast<char>(ch));
            ++pos;
        }
        if (!current.empty()) {
            tokens.push_back(current);
        }

        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i] == "begincodespacerange" && i > 0) {
                int count = std::atoi(tokens[i - 1].c_str());
                for (int j = 0; j < count && i + 2 < tokens.size(); ++j) {
                    std::string start_hex = tokens[++i];
                    std::string end_hex = tokens[++i];
                    if (start_hex.size() >= 2 && start_hex.front() == '<' && start_hex.back() == '>') {
                        decoder.code_bytes = std::max(decoder.code_bytes,
                            static_cast<int>((start_hex.size() - 2) / 2));
                    }
                    (void)end_hex;
                }
                continue;
            }
            if (tokens[i] == "beginbfchar" && i > 0) {
                int count = std::atoi(tokens[i - 1].c_str());
                for (int j = 0; j < count && i + 2 < tokens.size(); ++j) {
                    std::string src = tokens[++i];
                    std::string dst = tokens[++i];
                    if (src.size() >= 2 && src.front() == '<' && src.back() == '>' &&
                        dst.size() >= 2 && dst.front() == '<' && dst.back() == '>') {
                        std::string src_hex = src.substr(1, src.size() - 2);
                        std::string dst_hex = dst.substr(1, dst.size() - 2);
                        decoder.code_bytes = std::max(decoder.code_bytes,
                            static_cast<int>(src_hex.size() / 2));
                        decoder.glyph_to_unicode[hex_to_uint(src_hex)] = hex_utf16be_to_utf8(dst_hex);
                    }
                }
                continue;
            }
            if (tokens[i] == "beginbfrange" && i > 0) {
                int count = std::atoi(tokens[i - 1].c_str());
                for (int j = 0; j < count && i + 3 < tokens.size(); ++j) {
                    std::string start = tokens[++i];
                    std::string end = tokens[++i];
                    std::string dst = tokens[++i];
                    if (start.size() < 2 || end.size() < 2 ||
                        start.front() != '<' || start.back() != '>' ||
                        end.front() != '<' || end.back() != '>') {
                        continue;
                    }

                    std::string start_hex = start.substr(1, start.size() - 2);
                    std::string end_hex = end.substr(1, end.size() - 2);
                    unsigned int start_code = hex_to_uint(start_hex);
                    unsigned int end_code = hex_to_uint(end_hex);
                    decoder.code_bytes = std::max(decoder.code_bytes,
                        static_cast<int>(start_hex.size() / 2));

                    if (dst == "[") {
                        unsigned int code = start_code;
                        while (i + 1 < tokens.size() && tokens[i + 1] != "]" && code <= end_code) {
                            std::string item = tokens[++i];
                            if (item.size() >= 2 && item.front() == '<' && item.back() == '>') {
                                decoder.glyph_to_unicode[code] = hex_utf16be_to_utf8(
                                    item.substr(1, item.size() - 2));
                                ++code;
                            }
                        }
                        if (i + 1 < tokens.size() && tokens[i + 1] == "]") {
                            ++i;
                        }
                        continue;
                    }

                    if (dst.size() >= 2 && dst.front() == '<' && dst.back() == '>') {
                        std::vector<unsigned char> base_bytes = hex_to_bytes(
                            dst.substr(1, dst.size() - 2));
                        unsigned int current_codepoint = 0;
                        if (base_bytes.size() >= 2) {
                            current_codepoint = (static_cast<unsigned int>(base_bytes[base_bytes.size() - 2]) << 8) |
                                                static_cast<unsigned int>(base_bytes[base_bytes.size() - 1]);
                        }
                        for (unsigned int code = start_code; code <= end_code; ++code) {
                            std::vector<unsigned char> bytes = base_bytes;
                            if (bytes.size() >= 2) {
                                bytes[bytes.size() - 2] = static_cast<unsigned char>((current_codepoint >> 8) & 0xFF);
                                bytes[bytes.size() - 1] = static_cast<unsigned char>(current_codepoint & 0xFF);
                            }
                            decoder.glyph_to_unicode[code] = utf16be_bytes_to_utf8(bytes);
                            ++current_codepoint;
                        }
                    }
                }
            }
        }

        return decoder;
    }

    std::unordered_map<std::string, FontDecoder> build_font_map(const PdfValue &resources_value) {
        std::unordered_map<std::string, FontDecoder> out;
        const std::unordered_map<std::string, PdfValue> *resources = resolve_dict(resources_value);
        if (resources == nullptr) {
            return out;
        }
        std::unordered_map<std::string, PdfValue>::const_iterator fonts_it = resources->find("Font");
        if (fonts_it == resources->end()) {
            return out;
        }
        const std::unordered_map<std::string, PdfValue> *fonts = resolve_dict(fonts_it->second);
        if (fonts == nullptr) {
            return out;
        }

        for (std::unordered_map<std::string, PdfValue>::const_iterator it = fonts->begin();
             it != fonts->end(); ++it) {
            const std::string &font_name = it->first;
            const PdfValue *font_value = resolve_value(it->second);
            if (font_value == nullptr || font_value->type != PdfValue::kDict) {
                continue;
            }
            std::unordered_map<std::string, PdfValue>::const_iterator to_unicode_it =
                font_value->dict_value.find("ToUnicode");
            if (to_unicode_it == font_value->dict_value.end() || to_unicode_it->second.type != PdfValue::kRef) {
                continue;
            }
            PdfObject &cmap_object = load_object(to_unicode_it->second.ref_value);
            if (!cmap_object.has_stream) {
                continue;
            }
            out[font_name] = parse_to_unicode_cmap(decode_stream_bytes(cmap_object));
        }
        return out;
    }

    struct ContentToken {
        enum Type {
            kOperator,
            kName,
            kNumber,
            kLiteralString,
        kHexString,
        kDict,
        kArrayStart,
        kArrayEnd
        } type = kOperator;

        std::string text;
        double number = 0.0;
    };

    ContentToken next_content_token(const std::string &stream, size_t *pos) {
        skip_ws_and_comments(stream, pos);
        if (*pos >= stream.size()) {
            return ContentToken();
        }

        char ch = stream[*pos];
        if (ch == '[') {
            ++(*pos);
            ContentToken token;
            token.type = ContentToken::kArrayStart;
            token.text = "[";
            return token;
        }
        if (ch == ']') {
            ++(*pos);
            ContentToken token;
            token.type = ContentToken::kArrayEnd;
            token.text = "]";
            return token;
        }
        if (ch == '/') {
            ContentToken token;
            token.type = ContentToken::kName;
            token.text = read_name(stream, pos);
            return token;
        }
        if (ch == '(') {
            ContentToken token;
            token.type = ContentToken::kLiteralString;
            token.text = read_literal_string(stream, pos);
            return token;
        }
        if (ch == '<' && !starts_with_at(stream, *pos, "<<")) {
            ContentToken token;
            token.type = ContentToken::kHexString;
            token.text = read_hex_string(stream, pos);
            return token;
        }
        if (starts_with_at(stream, *pos, "<<")) {
            parse_pdf_value(stream, pos);
            ContentToken token;
            token.type = ContentToken::kDict;
            token.text = "<<>>";
            return token;
        }
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '+' || ch == '-' || ch == '.') {
            size_t save = *pos;
            double value = 0.0;
            if (parse_number_token(stream, pos, &value)) {
                ContentToken token;
                token.type = ContentToken::kNumber;
                token.number = value;
                token.text = stream.substr(save, *pos - save);
                return token;
            }
        }

        size_t start = *pos;
        while (*pos < stream.size()) {
            unsigned char cur = static_cast<unsigned char>(stream[*pos]);
            if (is_space(cur) || cur == '[' || cur == ']' || cur == '<' || cur == '>' ||
                cur == '/' || cur == '(' || cur == ')') {
                break;
            }
            ++(*pos);
        }
        ContentToken token;
        token.type = ContentToken::kOperator;
        token.text = stream.substr(start, *pos - start);
        return token;
    }

    std::string decode_with_font(const std::string &hex, const FontDecoder *font) {
        if (font == nullptr || font->glyph_to_unicode.empty()) {
            return std::string();
        }
        std::vector<unsigned char> bytes = hex_to_bytes(hex);
        std::string out;
        int chunk = font->code_bytes > 0 ? font->code_bytes : 2;
        for (size_t i = 0; i + static_cast<size_t>(chunk) <= bytes.size(); i += static_cast<size_t>(chunk)) {
            unsigned int code = 0;
            for (int j = 0; j < chunk; ++j) {
                code = (code << 8) | bytes[i + static_cast<size_t>(j)];
            }
            std::unordered_map<unsigned int, std::string>::const_iterator it = font->glyph_to_unicode.find(code);
            if (it != font->glyph_to_unicode.end()) {
                out += it->second;
            }
        }
        return out;
    }

    void append_text(std::string *out, const std::string &chunk) {
        if (chunk.empty()) {
            return;
        }
        out->append(chunk);
    }

    void append_newline(std::string *out) {
        if (out->empty() || out->back() == '\n') {
            return;
        }
        out->push_back('\n');
    }

    std::string parse_content_stream(const std::string &stream,
                                     const std::unordered_map<std::string, FontDecoder> &fonts) {
        std::string out;
        std::vector<ContentToken> operands;
        std::string current_font_name;

        size_t pos = 0;
        while (pos < stream.size()) {
            ContentToken token = next_content_token(stream, &pos);
            if (token.text.empty() && token.type == ContentToken::kOperator) {
                break;
            }
            if (token.type != ContentToken::kOperator) {
                operands.push_back(token);
                continue;
            }

            const FontDecoder *font = nullptr;
            std::unordered_map<std::string, FontDecoder>::const_iterator fit = fonts.find(current_font_name);
            if (fit != fonts.end()) {
                font = &fit->second;
            }

            if (token.text == "Tf") {
                if (operands.size() >= 2 && operands[operands.size() - 2].type == ContentToken::kName) {
                    current_font_name = operands[operands.size() - 2].text;
                }
            } else if (token.text == "Tj") {
                if (!operands.empty()) {
                    const ContentToken &arg = operands.back();
                    if (arg.type == ContentToken::kHexString) {
                        append_text(&out, decode_with_font(arg.text, font));
                    } else if (arg.type == ContentToken::kLiteralString) {
                        append_text(&out, arg.text);
                    }
                }
            } else if (token.text == "TJ") {
                if (!operands.empty() && operands.back().type == ContentToken::kArrayEnd) {
                    ssize_t idx = static_cast<ssize_t>(operands.size()) - 2;
                    while (idx >= 0 && operands[static_cast<size_t>(idx)].type != ContentToken::kArrayStart) {
                        const ContentToken &arg = operands[static_cast<size_t>(idx)];
                        if (arg.type == ContentToken::kHexString) {
                            append_text(&out, decode_with_font(arg.text, font));
                        } else if (arg.type == ContentToken::kLiteralString) {
                            append_text(&out, arg.text);
                        } else if (arg.type == ContentToken::kNumber && arg.number < -120.0) {
                            append_text(&out, " ");
                        }
                        --idx;
                    }
                }
            } else if (token.text == "'" || token.text == "\"") {
                append_newline(&out);
                if (!operands.empty()) {
                    const ContentToken &arg = operands.back();
                    if (arg.type == ContentToken::kHexString) {
                        append_text(&out, decode_with_font(arg.text, font));
                    } else if (arg.type == ContentToken::kLiteralString) {
                        append_text(&out, arg.text);
                    }
                }
            } else if (token.text == "Td" || token.text == "TD") {
                if (operands.size() >= 2 &&
                    operands[operands.size() - 2].type == ContentToken::kNumber &&
                    operands[operands.size() - 1].type == ContentToken::kNumber) {
                    double y = operands[operands.size() - 1].number;
                    if (std::abs(y) > 0.01) {
                        append_newline(&out);
                    }
                }
            } else if (token.text == "T*" || token.text == "ET") {
                append_newline(&out);
            }

            operands.clear();
        }

        return out;
    }

    std::string normalize_text(const std::string &raw) {
        std::string out;
        bool previous_space = false;
        bool previous_newline = false;

        for (size_t i = 0; i < raw.size(); ++i) {
            unsigned char ch = static_cast<unsigned char>(raw[i]);
            if (ch == '\r') {
                continue;
            }
            if (ch == '\n') {
                while (!out.empty() && out.back() == ' ') {
                    out.pop_back();
                }
                if (previous_newline) {
                    continue;
                }
                out.push_back('\n');
                previous_space = false;
                previous_newline = true;
                continue;
            }
            if (std::isspace(ch)) {
                if (!previous_space && !previous_newline) {
                    out.push_back(' ');
                }
                previous_space = true;
                continue;
            }
            out.push_back(static_cast<char>(ch));
            previous_space = false;
            previous_newline = false;
        }

        while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) {
            out.pop_back();
        }
        return out;
    }
};

}  // namespace

ExtractResult extract_pdf_page_text_from_path(const std::string &pdf_path, int page_index) {
    ExtractResult result;
    try {
        PdfDocument document(read_file(pdf_path));
        document.load();
        result.text = document.extract_page_text(page_index);
        result.ok = !result.text.empty();
        if (!result.ok) {
            result.error = "no extractable text found on the requested PDF page";
        }
    } catch (const std::exception &error) {
        result.ok = false;
        result.error = error.what();
    }
    return result;
}

}  // namespace translate_helper
