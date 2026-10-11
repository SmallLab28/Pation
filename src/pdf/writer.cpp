#include <chrono>
/*
    writer.cpp - Pation PDF Rewriter / Optimiser (Production Grade)
    
    Properly handles:
      - Type-1 objects (direct byte-offset objects)
      - Type-2 objects (embedded in ObjStm containers) -- extracted and flattened
      - Image recompression (JPEG downscale for size reduction)
      - Traditional XRef table output (maximum compatibility)
*/
#include "pdf/writer.h"
#include "pdf/xref.h"
#include "pdf/font.h"
#include "pdf/stream.h"
#include "img/img.h"
#include "miniz/miniz.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../third-party/stb/stb_image.h"

#include <algorithm>
#include <regex>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../third-party/stb/stb_image_resize2.h"
#include <vector>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <map>
#include <sstream>
#include <set>

struct MemWriter {
    std::vector<uint8_t> data;
    static void write(void *context, void *data, int size) {
        MemWriter *w = (MemWriter*)context;
        uint8_t *b = (uint8_t*)data;
        w->data.insert(w->data.end(), b, b + size);
    }
};

static int g_image_objects = 0;
static int g_image_eligible = 0;
static int g_image_decoded = 0;
static int g_image_reencoded = 0;
static int g_image_fallback = 0;
static int g_image_dct = 0;
static int g_image_flate = 0;
static int g_image_jpx = 0;
static int g_image_ccitt = 0;
static int g_image_other = 0;
static int g_font_subset = 0;
static int g_skip_mask = 0;
static int g_skip_color = 0;
static int g_skip_decode = 0;
static int g_skip_filter = 0;
static std::vector<uint32_t> g_font_codepoints;
static void collect_tounicode_codepoints(const std::vector<uint8_t> &raw);
static std::vector<uint8_t> read_obj_raw(pt_context *ctx, long offset, long end_hint);
static std::string resolve_image_colorspace(pt_context *ctx, pt_state *st,
                                            const std::string &dict_str);
static std::vector<uint8_t> parse_palette(pt_context *ctx, pt_state *st, int cs_id);



static bool dict_has(const std::string &d, const char *key) {
    return d.find(key) != std::string::npos;
}
static long dict_int(const std::string &dict, const std::string &key, long def = 0) {
    size_t p = dict.find(key);
    if (p == std::string::npos) return def;
    p += key.size();
    while (p < dict.size() && isspace((unsigned char)dict[p])) p++;
    size_t start = p;
    // Check if this is an indirect reference "N G R"
    size_t q = start;
    while (q < dict.size() && isdigit((unsigned char)dict[q])) q++;
    size_t q2 = q;
    while (q2 < dict.size() && isspace((unsigned char)dict[q2])) q2++;
    size_t q3 = q2;
    while (q3 < dict.size() && isdigit((unsigned char)dict[q3])) q3++;
    size_t q4 = q3;
    while (q4 < dict.size() && isspace((unsigned char)dict[q4])) q4++;
    if (q4 < dict.size() && dict[q4] == 'R') return def; // indirect
    try { return std::stol(dict.substr(start)); } catch (...) { return def; }
}

static bool dict_direct_int(const std::string &dict, const std::string &key, long &value) {
    size_t p = dict.find(key);
    if (p == std::string::npos) return false;
    p += key.size();
    while (p < dict.size() && isspace((unsigned char)dict[p])) p++;
    size_t start = p;
    while (p < dict.size() && isdigit((unsigned char)dict[p])) p++;
    if (p == start) return false;
    size_t q = p;
    while (q < dict.size() && isspace((unsigned char)dict[q])) q++;
    if (q < dict.size() && isdigit((unsigned char)dict[q])) return false;
    try {
        value = std::stol(dict.substr(start, p - start));
        return value >= 0;
    } catch (...) {
        return false;
    }
}





static void unfilter_png(std::vector<uint8_t>& data, int width, int height, int bpp) {
    int stride = width * bpp;
    if (data.size() < (size_t)height * (stride + 1)) return;
    uint8_t *prev_row = nullptr;
    for (int y = 0; y < height; y++) {
        uint8_t *row = data.data() + y * (stride + 1);
        int filter = row[0];
        uint8_t *pixels = row + 1;
        for (int x = 0; x < stride; x++) {
            uint8_t left = (x >= bpp) ? pixels[x - bpp] : 0;
            uint8_t up = prev_row ? prev_row[x] : 0;
            uint8_t upleft = (prev_row && x >= bpp) ? prev_row[x - bpp] : 0;
            if (filter == 1) pixels[x] += left;
            else if (filter == 2) pixels[x] += up;
            else if (filter == 3) pixels[x] += (left + up) / 2;
            else if (filter == 4) {
                int p = left + up - upleft;
                int pa = abs(p - left), pb = abs(p - up), pc = abs(p - upleft);
                if (pa <= pb && pa <= pc) pixels[x] += left;
                else if (pb <= pc) pixels[x] += up;
                else pixels[x] += upleft;
            }
        }
        memmove(data.data() + y * stride, pixels, stride);
        prev_row = data.data() + y * stride;
    }
    data.resize(height * stride);
}


// we use miniz instead? miniz.h?
#include "miniz/miniz.h"

static std::vector<uint8_t> flate_encode(const std::vector<uint8_t>& data) {
    mz_ulong dest_len = compressBound(data.size());
    std::vector<uint8_t> dest(dest_len);
    if (compress(dest.data(), &dest_len, data.data(), data.size()) == MZ_OK) {
        dest.resize(dest_len);
        return dest;
    }
    return {};
}

// 1. extract_dict
static std::string extract_dict(const std::vector<uint8_t>& raw) {
    if (raw.size() < 2) return "";
    size_t start = 0;
    for (size_t i = 0; i < raw.size() - 1; ++i) {
        if (raw[i] == '<' && raw[i+1] == '<') {
            start = i;
            break;
        }
    }
    if (start == 0 && (raw[0] != '<' || raw[1] != '<')) return "";

    int depth = 0;
    size_t end = start;
    while (end + 1 < raw.size()) {
        if (raw[end] == '<' && raw[end+1] == '<') {
            depth++;
            end += 2;
        } else if (raw[end] == '>' && raw[end+1] == '>') {
            depth--;
            end += 2;
            if (depth == 0) return std::string(raw.begin() + start, raw.begin() + end);
        } else {
            end++;
        }
    }
    return std::string(raw.begin() + start, raw.end());
}

static bool extract_stream(const std::vector<uint8_t>& raw, const uint8_t **sd, size_t *sl) {
    std::string s(raw.begin(), raw.end());
    size_t p = s.find("stream");
    if (p == std::string::npos) return false;
    p += 6;
    if (p < s.size() && s[p] == '\r') p++;
    if (p < s.size() && s[p] == '\n') p++;
    
    long length = 0;
    std::string dict_str = extract_dict(raw);
    if (dict_direct_int(dict_str, "/Length", length) && length >= 0 && p + length <= raw.size()) {
        size_t end = p + length;
        *sd = raw.data() + p;
        *sl = length;
        return true;
    }

    size_t end = s.find("endstream", p);
    if (end == std::string::npos) end = raw.size();
    else {
        if (end > p && s[end-1] == '\n') end--;
        if (end > p && s[end-1] == '\r') end--;
    }
    *sd = raw.data() + p;
    *sl = end - p;
    return true;
}
static std::vector<uint8_t> flate_decompress(const uint8_t *data, size_t len) {
    size_t out_len = 0;
    void *out = tinfl_decompress_mem_to_heap(data, len, &out_len, TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (!out) return {};
    std::vector<uint8_t> res((uint8_t*)out, (uint8_t*)out + out_len);
    mz_free(out);
    return res;
}

// 4. read_obj_raw
static std::vector<uint8_t> read_obj_raw(pt_context *ctx, long offset, long end_hint) {
    ctx->file->seek(ctx, ctx->file->f, offset, PT_SEEK_SET);
    long len = end_hint - offset;
    if (len <= 0 || len > 100*1024*1024) len = 100000;
    std::vector<uint8_t> buf(len);
    size_t n = ctx->file->read(ctx, (char*)buf.data(), 1, len, ctx->file->f);
    buf.resize(n);
    return buf;
}

// 5. parse_stream_bounds
static bool parse_stream_bounds(const std::vector<uint8_t>& raw, std::string& dict_str, const uint8_t*& sd, size_t& sl) {
    dict_str = extract_dict(raw);
    return extract_stream(raw, &sd, &sl);
}

// 6. extract_objstm
#include "pdf/parser.h"
static std::map<int, std::string> extract_objstm(const std::vector<uint8_t>& raw) {
    std::map<int, std::string> res;
    std::string dict_str = extract_dict(raw);
    const uint8_t *stream_data = nullptr;
    size_t stream_len = 0;
    if (!extract_stream(raw, &stream_data, &stream_len)) return res;

    long object_count = dict_int(dict_str, "/N", -1);
    long first_offset = dict_int(dict_str, "/First", -1);
    if (object_count <= 0 || first_offset < 0 ||
        dict_str.find("/FlateDecode") == std::string::npos) {
        return res;
    }

    std::vector<uint8_t> decoded = flate_decompress(stream_data, stream_len);
    if (decoded.empty() || first_offset >= (long)decoded.size()) return res;

    std::string header((const char *)decoded.data(), (size_t)first_offset);
    std::istringstream header_stream(header);
    std::vector<std::pair<int, size_t>> entries;
    for (long i = 0; i < object_count; ++i) {
        int object_id;
        size_t object_offset;
        if (!(header_stream >> object_id >> object_offset)) return {};
        entries.push_back({object_id, object_offset});
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        size_t begin = (size_t)first_offset + entries[i].second;
        size_t end = decoded.size();
        if (i + 1 < entries.size()) {
            end = (size_t)first_offset + entries[i + 1].second;
        }
        if (begin > end || end > decoded.size()) return {};
        while (begin < end && isspace(decoded[begin])) begin++;
        while (end > begin && isspace(decoded[end - 1])) end--;
        res[entries[i].first] = std::string((const char *)decoded.data() + begin, end - begin);
    }
    return res;
}

// 7. remove_dict_entry
static void remove_dict_entry(std::string &dict_str, const std::string &key) {
    size_t search_pos = 0;
    while (true) {
        size_t pos = dict_str.find(key, search_pos);
        if (pos == std::string::npos) return;
        
        bool exact = true;
        if (pos + key.size() < dict_str.size()) {
            char c = dict_str[pos + key.size()];
            if (isalnum((unsigned char)c)) exact = false;
        }
        if (!exact) {
            search_pos = pos + key.size();
            continue;
        }

        size_t end = pos + key.size();
        while (end < dict_str.size() && isspace((unsigned char)dict_str[end])) end++;
        
        if (end >= dict_str.size()) {
            dict_str.erase(pos);
            return;
        }

        if (dict_str[end] == '[') {
            int depth = 1;
            end++;
            while (end < dict_str.size() && depth > 0) {
                if (dict_str[end] == '[') depth++;
                else if (dict_str[end] == ']') depth--;
                end++;
            }
        } else if (dict_str[end] == '<' && end + 1 < dict_str.size() && dict_str[end+1] == '<') {
            int depth = 1;
            end += 2;
            while (end + 1 < dict_str.size() && depth > 0) {
                if (dict_str[end] == '<' && dict_str[end+1] == '<') { depth++; end += 2; continue; }
                if (dict_str[end] == '>' && dict_str[end+1] == '>') { depth--; end += 2; continue; }
                end++;
            }
        } else if (dict_str[end] == '/') {
            end++;
            while (end < dict_str.size() && !isspace((unsigned char)dict_str[end]) && dict_str[end] != '/' && dict_str[end] != '[' && dict_str[end] != '<' && dict_str[end] != '>') end++;
        } else if (dict_str[end] == '(') {
            int depth = 1;
            end++;
            while (end < dict_str.size() && depth > 0) {
                if (dict_str[end] == '\\') end += 2;
                else if (dict_str[end] == '(') { depth++; end++; }
                else if (dict_str[end] == ')') { depth--; end++; }
                else end++;
            }
        } else {
            while (end < dict_str.size() && !isspace((unsigned char)dict_str[end]) && dict_str[end] != '/' && dict_str[end] != '[' && dict_str[end] != '<' && dict_str[end] != '>') end++;
            
            size_t temp_end = end;
            while (temp_end < dict_str.size() && isspace((unsigned char)dict_str[temp_end])) temp_end++;
            size_t gen_start = temp_end;
            while (temp_end < dict_str.size() && isdigit((unsigned char)dict_str[temp_end])) temp_end++;
            if (temp_end > gen_start) {
                while (temp_end < dict_str.size() && isspace((unsigned char)dict_str[temp_end])) temp_end++;
                if (temp_end < dict_str.size() && dict_str[temp_end] == 'R') {
                    end = temp_end + 1;
                }
            }
        }
        
        dict_str.erase(pos, end - pos);
        return;
    }
}

// 8. dict_has
static bool dict_has(const std::string &dict_str, const std::string &key) {
    size_t search_pos = 0;
    while (true) {
        size_t pos = dict_str.find(key, search_pos);
        if (pos == std::string::npos) return false;
        if (pos + key.size() < dict_str.size()) {
            char c = dict_str[pos + key.size()];
            if (isalnum((unsigned char)c)) {
                search_pos = pos + key.size();
                continue;
            }
        }
        return true;
    }
}

static bool image_can_downsample(const std::string &dict_str) {
    if (dict_str.find("/ImageMask true") != std::string::npos ||
        dict_str.find("/SMask") != std::string::npos ||
        dict_str.find("/Mask") != std::string::npos ||
        dict_str.find("/Decode") != std::string::npos ||
        dict_str.find("/Intent") != std::string::npos ||
        dict_str.find("/Indexed") != std::string::npos ||
        dict_str.find("/Separation") != std::string::npos ||
        dict_str.find("/DeviceN") != std::string::npos ||
        dict_str.find("/ICCBased") != std::string::npos) {
        return false;
    }

    long bits_per_component = dict_int(dict_str, "/BitsPerComponent", 8);
    if (bits_per_component != 1 && bits_per_component != 8) return false;

    long predictor = dict_int(dict_str, "/Predictor", 1);
    if (predictor != 1 && (predictor < 10 || predictor > 15)) return false;

    bool supported_color_space = dict_str.find("/DeviceGray") != std::string::npos ||
                                 dict_str.find("/DeviceRGB") != std::string::npos ||
                                 dict_str.find("/DeviceCMYK") != std::string::npos;
    if (!supported_color_space) return false;

    return dict_str.find("/DCTDecode") != std::string::npos ||
           dict_str.find("/FlateDecode") != std::string::npos;
}

struct FlatObj {
    int id;
    int gen;
    int obj_id; // added for my code
    std::vector<uint8_t> data;
};


FlatObj process_stream_obj(pt_context *ctx, pt_state *st, int obj_id, int gen, const std::vector<uint8_t>& raw) {
    FlatObj fo;
    fo.id = obj_id;
    fo.obj_id = obj_id;
    fo.gen = gen;
    std::string dict_str = extract_dict(raw);
    const uint8_t *stream_data = nullptr;
    size_t stream_len = 0;
    bool ext_res = extract_stream(raw, &stream_data, &stream_len);
    if (!ext_res) {
        fo.data = raw;
        return fo;
    }

    bool is_image = dict_str.find("/Subtype /Image") != std::string::npos ||
                    dict_str.find("/Subtype/Image") != std::string::npos;
    if (!is_image) {
        bool is_font_program = dict_str.find("/Length1") != std::string::npos;
        if (is_font_program && !g_font_codepoints.empty()) {
            std::vector<uint8_t> font_bytes = dict_str.find("/FlateDecode") != std::string::npos
                ? flate_decompress(stream_data, stream_len)
                : std::vector<uint8_t>(stream_data, stream_data + stream_len);
            bool is_truetype = font_bytes.size() >= 4 &&
                               ((font_bytes[0] == 0 && font_bytes[1] == 1 &&
                                 font_bytes[2] == 0 && font_bytes[3] == 0) ||
                                (font_bytes[0] == 't' && font_bytes[1] == 'r' &&
                                 font_bytes[2] == 'u' && font_bytes[3] == 'e'));
            if (is_truetype) {
                pt_font font = {};
                size_t subset_size = 0;
                if (pt_font_open(&font, font_bytes.data(), font_bytes.size()) == 0) {
                    pt_glyph_set glyphs = pt_glyph_set_create(font.num_glyphs);
                    pt_cmap_mark(&font, &glyphs, g_font_codepoints.data(), g_font_codepoints.size());
                    pt_glyf_closure(&font, &glyphs);
                    uint8_t *subset = pt_font_subset(&font, &glyphs, &subset_size);
                pt_glyph_set_free(&glyphs);
                pt_font_close(&font);
                if (subset && subset_size > 0 && subset_size < font_bytes.size()) {
                    remove_dict_entry(dict_str, "/Filter");
                    remove_dict_entry(dict_str, "/DecodeParms");
                    remove_dict_entry(dict_str, "/Length");
                    remove_dict_entry(dict_str, "/Length1");
                    size_t dd = dict_str.rfind(">>");
                    if (dd == std::string::npos) dd = dict_str.size();
                    dict_str.insert(dd, "\n/Length " + std::to_string(subset_size) +
                                         "\n/Length1 " + std::to_string(subset_size));
                    std::string header = std::to_string(obj_id) + " " + std::to_string(gen) + " obj\n" + dict_str;
                    if (!header.empty() && header.back() != '\n' && header.back() != '\r') header += "\n";
                    header += "stream\n";
                    fo.data.assign(header.begin(), header.end());
                    fo.data.insert(fo.data.end(), subset, subset + subset_size);
                    const char *tail = "\nendstream\nendobj\n";
                    fo.data.insert(fo.data.end(), tail, tail + strlen(tail));
                    std::free(subset);
                    g_font_subset++;


                    return fo;
                }
                std::free(subset);
            }
            } // Close if (is_truetype)
        } // Close if (is_font_program ...)
        if (dict_str.find("/Filter") == std::string::npos && stream_len > 64) {
            auto t_encode_start = std::chrono::high_resolution_clock::now();
            std::vector<uint8_t> uncompressed(stream_data, stream_data + stream_len);
            std::vector<uint8_t> compressed = flate_encode(uncompressed);
            auto t_encode_end = std::chrono::high_resolution_clock::now();
            if (!compressed.empty() && compressed.size() < uncompressed.size()) {
                remove_dict_entry(dict_str, "/Length");
                size_t dd = dict_str.rfind(">>");
                if (dd == std::string::npos) dd = dict_str.size();
                dict_str.insert(dd, "\n/Filter /FlateDecode\n/Length " + std::to_string(compressed.size()) + "\n");
                std::string header = std::to_string(obj_id) + " " + std::to_string(gen) + " obj\n" + dict_str;
                if (!header.empty() && header.back() != '\n' && header.back() != '\r') header += "\n";
                header += "stream\n";
                fo.data.assign(header.begin(), header.end());
                fo.data.insert(fo.data.end(), compressed.begin(), compressed.end());
                const char *tail = "\nendstream\nendobj\n";
                fo.data.insert(fo.data.end(), tail, tail + strlen(tail));
                return fo;
            }
        }
        fo.data = raw;
        return fo;
    }
    auto t_image_start = std::chrono::high_resolution_clock::now();
    g_image_objects++;
    if (dict_str.find("/DCTDecode") != std::string::npos) g_image_dct++;
    else if (dict_str.find("/FlateDecode") != std::string::npos) g_image_flate++;
    else if (dict_str.find("/JPXDecode") != std::string::npos) g_image_jpx++;
    else if (dict_str.find("/CCITTFaxDecode") != std::string::npos) g_image_ccitt++;
    else g_image_other++;

    {

        auto tp1 = std::chrono::high_resolution_clock::now();
        long w = dict_int(dict_str, "/Width", -1);
        long h = dict_int(dict_str, "/Height", -1);
        int img_channels = 3;
        if (dict_str.find("/DeviceGray") != std::string::npos) img_channels = 1;
        else if (dict_str.find("/DeviceCMYK") != std::string::npos) img_channels = 4;
        
        long decode_colors = dict_int(dict_str, "/Colors", -1);
        if (decode_colors >= 1 && decode_colors <= 4) {
            img_channels = decode_colors;
        }
        
        long decode_columns = dict_int(dict_str, "/Columns", -1);
        if (decode_columns > 0 && decode_columns != w) {
            w = decode_columns;
        }

        if (w > 0 && h > 0) {
            bool was_dct = (dict_str.find("/DCTDecode") != std::string::npos);
            bool was_flate = (dict_str.find("/FlateDecode") != std::string::npos);
            long bpc = dict_int(dict_str, "/BitsPerComponent", 8);
            bool has_mask = dict_str.find("/Mask") != std::string::npos ||
                            dict_str.find("/SMask") != std::string::npos ||
                            dict_str.find("/ImageMask") != std::string::npos;
            bool supported_filter = was_dct || was_flate;
            bool supported_depth = bpc == 1 || bpc == 8;
            std::string real_cs = dict_str;
            int cs_id = -1;
            if (dict_str.find("/ColorSpace") != std::string::npos) {
                size_t cs_pos = dict_str.find("/ColorSpace");
                std::regex ref_pattern(R"((\d+)\s+(\d+)\s+R)");
                std::smatch match;
                std::string sub = dict_str.substr(cs_pos, std::min(dict_str.size() - cs_pos, (size_t)100));
                if (std::regex_search(sub, match, ref_pattern)) cs_id = std::stoi(match[1].str());
                std::string resolved = resolve_image_colorspace(ctx, st, dict_str);
                if (!resolved.empty()) real_cs = resolved;
            }
            bool is_indexed = (real_cs.find("/Indexed") != std::string::npos);
            bool is_devicen_cyk = false;
            if (is_indexed) {
                if (real_cs.find("Cyan") != std::string::npos && real_cs.find("Yellow") != std::string::npos) {
                    is_devicen_cyk = true;
                } else {
                    std::regex base_ref_pattern(R"(/Indexed\s+(\d+)\s+(\d+)\s+R)");
                    std::smatch base_match;
                    if (std::regex_search(real_cs, base_match, base_ref_pattern)) {
                        int base_id = std::stoi(base_match[1].str());
                        long base_offset = st->xref->look(ctx, st, base_id);
                        if (base_offset > 0) {
                            std::vector<uint8_t> base_raw = read_obj_raw(ctx, base_offset, base_offset + 2048);
                            std::string base_str(base_raw.begin(), base_raw.end());
                            if (base_str.find("/DeviceN") != std::string::npos &&
                                base_str.find("Cyan") != std::string::npos &&
                                base_str.find("Yellow") != std::string::npos) {
                                is_devicen_cyk = true;
                            }
                        }
                    }
                }
            }
            bool indexed_or_special = real_cs.find("/Separation") != std::string::npos ||
                                      (real_cs.find("/DeviceN") != std::string::npos && !is_devicen_cyk) ||
                                      (is_indexed && real_cs.find("/DeviceRGB") == std::string::npos && !is_devicen_cyk);
            bool is_cmyk = (real_cs.find("/DeviceCMYK") != std::string::npos);
            // Aggressive mode: force ordinary DCT/Flate images through the
            // codec; retain only masks/special indexed images for fidelity.
            bool skip_image = has_mask || !supported_filter || !supported_depth || indexed_or_special || bpc == 1;
            if (skip_image) {
                if (dict_str.find("/Mask") != std::string::npos ||
                    dict_str.find("/SMask") != std::string::npos ||
                    dict_str.find("/ImageMask") != std::string::npos) g_skip_mask++;
                if (dict_str.find("/Indexed") != std::string::npos ||
                    dict_str.find("/Separation") != std::string::npos ||
                    dict_str.find("/DeviceN") != std::string::npos ||
                    dict_str.find("/ICCBased") != std::string::npos) g_skip_color++;
                if (dict_str.find("/Decode") != std::string::npos ||
                    dict_str.find("/Intent") != std::string::npos) g_skip_decode++;
                if (dict_str.find("/DCTDecode") == std::string::npos &&
                    dict_str.find("/FlateDecode") == std::string::npos) g_skip_filter++;
            }
            if (!skip_image) g_image_eligible++;

            std::vector<uint8_t> raw_pixels;
            int img_w = w, img_h = h;

            if (was_dct && is_cmyk) {
                int out_w = 0, out_h = 0;
                uint8_t *cmyk_pixels = pt_cmyk_jpeg_decode(stream_data, stream_len, &out_w, &out_h);
                if (!cmyk_pixels) {
                    skip_image = true;
                    goto raw_fallback;
                }
                img_w = out_w;
                img_h = out_h;
                img_channels = 4;
                raw_pixels.assign(cmyk_pixels, cmyk_pixels + (size_t)img_w * img_h * 4);
                std::free(cmyk_pixels);
                w = img_w; h = img_h;
                g_image_decoded++;
            } else if (was_dct) {
                int out_w = 0, out_h = 0, out_channels = 0;
                unsigned char *stb_pixels = stbi_load_from_memory(stream_data, (int)stream_len, &out_w, &out_h, &out_channels, 0);
                if (!stb_pixels) {
                    skip_image = true;
                    goto raw_fallback;
                }
                
                img_w = out_w;
                img_h = out_h;
                img_channels = out_channels;
                if (img_channels != 1 && img_channels != 3 && img_channels != 4) {
                     stbi_image_free(stb_pixels);
                     skip_image = true;
                     goto raw_fallback;
                }

                raw_pixels.assign(stb_pixels, stb_pixels + img_w * img_h * img_channels);
                stbi_image_free(stb_pixels);
                
                w = img_w; h = img_h;
                g_image_decoded++;
            } else {
            raw_fallback:
                auto tp3 = std::chrono::high_resolution_clock::now();
                if (was_flate && !skip_image) {
                    int decoded_components = real_cs.find("/DeviceGray") != std::string::npos ? 1 :
                                             real_cs.find("/DeviceCMYK") != std::string::npos ? 4 : 
                                             real_cs.find("/N 1") != std::string::npos ? 1 :
                                             real_cs.find("/N 4") != std::string::npos ? 4 : 3;
                    if (is_indexed) decoded_components = 1;
                    long decode_colors_for_pred = dict_int(dict_str, "/Colors", -1);
                    if (decode_colors_for_pred >= 1 && decode_colors_for_pred <= 4) {
                        decoded_components = decode_colors_for_pred;
                    }
                    int predictor = (int)dict_int(dict_str, "/Predictor", 1);
                    if (w == 639 && h == 613) {
                    }
                    
                    pt_image decoded = pt_flate_decode(stream_data, stream_len, (int)w, (int)h,
                                                       decoded_components, (int)bpc, predictor);
                                                       
                    if (decoded.pixels && decoded.width > 0 && decoded.height > 0) {
                        if (is_indexed && cs_id > 0) {
                            std::vector<uint8_t> pal = parse_palette(ctx, st, cs_id);
                            if (!pal.empty()) {
                                std::vector<uint8_t> rgb_pal;
                                size_t num_colors = pal.size() / 3;
                                if (is_devicen_cyk) {
                                    rgb_pal.resize(num_colors * 3);
                                    for (size_t c_idx = 0; c_idx < num_colors; c_idx++) {
                                        float c = pal[c_idx * 3 + 0] / 255.0f;
                                        float y = pal[c_idx * 3 + 1] / 255.0f;
                                        float k = pal[c_idx * 3 + 2] / 255.0f;
                                        // Calibrated pigment cross-talk model for DeviceN(Cyan, Yellow, Black):
                                        // Real inks absorb green light when layered, preventing neon oversaturation.
                                        float r = (1.0f - c) * (1.0f - k) + (0.24f * y) * (1.0f - k);
                                        float g = (1.0f - k) * (1.0f - 0.12f * c - 0.11f * y);
                                        float b = (1.0f - y) * (1.0f - k) + (0.55f * c) * (1.0f - k);
                                        if (r < 0.0f) r = 0.0f; if (r > 1.0f) r = 1.0f;
                                        if (g < 0.0f) g = 0.0f; if (g > 1.0f) g = 1.0f;
                                        if (b < 0.0f) b = 0.0f; if (b > 1.0f) b = 1.0f;
                                        rgb_pal[c_idx * 3 + 0] = (uint8_t)(r * 255.0f + 0.5f);
                                        rgb_pal[c_idx * 3 + 1] = (uint8_t)(g * 255.0f + 0.5f);
                                        rgb_pal[c_idx * 3 + 2] = (uint8_t)(b * 255.0f + 0.5f);
                                    }
                                }
                                const uint8_t *use_pal = is_devicen_cyk ? rgb_pal.data() : pal.data();
                                size_t pal_size = is_devicen_cyk ? rgb_pal.size() : pal.size();

                                uint8_t *rgb = (uint8_t*)malloc((size_t)decoded.width * decoded.height * 3);
                                for (long i = 0; i < (long)decoded.width * decoded.height; i++) {
                                    uint8_t idx = decoded.pixels[i];
                                    if ((size_t)idx * 3 + 2 < pal_size) {
                                        rgb[i*3+0] = use_pal[idx*3+0];
                                        rgb[i*3+1] = use_pal[idx*3+1];
                                        rgb[i*3+2] = use_pal[idx*3+2];
                                    } else {
                                        rgb[i*3+0] = rgb[i*3+1] = rgb[i*3+2] = idx;
                                    }
                                }
                                free(decoded.pixels);
                                decoded.pixels = rgb;
                                decoded.components = 3;
                                decoded.cs = PT_CS_RGB;
                                std::regex cs_replace(R"(/ColorSpace\s+\d+\s+\d+\s+R)");
                                dict_str = std::regex_replace(dict_str, cs_replace, "/ColorSpace /DeviceRGB");
                            }
                        }
                        
                        raw_pixels.assign(decoded.pixels,
                                          decoded.pixels + (size_t)decoded.width * decoded.height * decoded.components);
                        img_channels = decoded.components;
                        pt_img_free(&decoded);
                        g_image_decoded++;
                    } else {
                        skip_image = true;
                    }
                } else if (!was_flate) {
                    raw_pixels.assign(stream_data, stream_data + stream_len);
                }
            }

            
            bool is_line_art = false;
            if (!raw_pixels.empty() && img_channels == 1) {
                long non_bw = 0;
                long total = raw_pixels.size();
                for (long i = 0; i < total; i++) {
                    uint8_t v = raw_pixels[i];
                    if (v > 15 && v < 240) non_bw++;
                }
                if (total > 0 && non_bw < total / 50) { // less than 2% gray pixels
                    is_line_art = true;
                }
            }
            if (is_line_art) {
                skip_image = true;
            }

            bool is_grayscale = true;
            if (img_channels == 4) is_grayscale = false;
            else if (img_channels >= 3 && !raw_pixels.empty()) {
                int color_pixels = 0;
                for (size_t i = 0; i < raw_pixels.size(); i += img_channels) {
                    int r = raw_pixels[i];
                    int g = raw_pixels[i+1];
                    int b = raw_pixels[i+2];
                    if (abs(r - g) > 10 || abs(r - b) > 10) {
                        color_pixels++;
                        if (color_pixels > 100) { is_grayscale = false; break; }
                    }
                }
            }

            if (is_grayscale && img_channels >= 3 && !raw_pixels.empty()) {
                std::vector<uint8_t> gray_pixels(w * h);
                for (long i = 0; i < w * h; i++) {
                    gray_pixels[i] = raw_pixels[i * img_channels];
                }
                raw_pixels = std::move(gray_pixels);
                img_channels = 1;
            }


            std::vector<uint8_t> out_jpeg;
            std::vector<uint8_t> out_flate;
            long new_w = w, new_h = h;
            bool is_binarized = false;
            
            if (!skip_image && !raw_pixels.empty()) {
                int target_dpi = (st->writer && st->writer->target_dpi > 0) ? st->writer->target_dpi : 150;
                if (target_dpi < 150) target_dpi = 150; 

                int target_max_dim = (target_dpi <= 150) ? 1000 : 1500;
                int max_dim = std::max(w, h);
                double scale = (max_dim > target_max_dim) ? (double)target_max_dim / max_dim : 1.0;
                
                if (scale < 1.0 && (was_dct || is_devicen_cyk)) {
                    new_w = (long)(w * scale);
                    new_h = (long)(h * scale);
                }
            
            if (new_w < 16) new_w = std::min((long)w, 16L);
            if (new_h < 16) new_h = std::min((long)h, 16L);

            stbir_pixel_layout resize_layout = (img_channels == 1) ? STBIR_1CHANNEL :
                                               (img_channels == 3) ? STBIR_RGB :
                                               (img_channels == 4) ? STBIR_4CHANNEL : STBIR_1CHANNEL;

            std::vector<uint8_t> binarized_pixels;
            if (is_line_art && img_channels == 1 && !raw_pixels.empty()) {
                size_t row_bytes = (new_w + 7) / 8;
                binarized_pixels.resize(row_bytes * new_h, 0);
                
                std::vector<uint8_t> temp_scaled;
                uint8_t* src_pixels = raw_pixels.data();
                if (scale < 1.0 && was_dct) {
                    temp_scaled.resize(new_w * new_h * img_channels);
                    stbir_resize_uint8_linear(raw_pixels.data(), (int)w, (int)h, 0, temp_scaled.data(), (int)new_w, (int)new_h, 0, resize_layout);
                    src_pixels = temp_scaled.data();
                }

                for (long y = 0; y < new_h; y++) {
                    for (long x = 0; x < new_w; x++) {
                        uint8_t v = src_pixels[y * new_w + x];
                        if (v > 127) {
                            binarized_pixels[y * row_bytes + x / 8] |= (1 << (7 - (x % 8)));
                        }
                    }
                }
                is_binarized = true;
            }

            std::vector<uint8_t> scaled_pixels;
            if (!is_binarized && (new_w != w || new_h != h)) {
                scaled_pixels.resize(new_w * new_h * img_channels);
                stbir_resize_uint8_linear(raw_pixels.data(), (int)w, (int)h, 0, scaled_pixels.data(), (int)new_w, (int)new_h, 0, resize_layout);
            } else if (!is_binarized) {
                scaled_pixels = raw_pixels;
            } else {
                scaled_pixels = binarized_pixels;
            }

                pt_image image = {};
                image.pixels = scaled_pixels.data();
                image.width = (int)new_w;
                image.height = (int)new_h;
                image.components = img_channels;
                image.cs = img_channels == 1 ? PT_CS_GRAY :
                           img_channels == 4 ? PT_CS_CMYK : PT_CS_RGB;
                           
                int target_quality = (st && st->writer && st->writer->target_quality > 0) ? st->writer->target_quality : 50;
                if (target_quality < 10) target_quality = 10;
                pt_compressed compressed = {nullptr, 0};
                bool use_flate = is_binarized;
                if (use_flate || is_binarized) {
                    size_t raw_len = is_binarized ? ((size_t)image.width + 7) / 8 * image.height : (size_t)image.width * image.height * image.components;
                    compressed = pt_flate_encode(image.pixels, raw_len);
                    use_flate = true; // force flate for binarized
                } else {
                    compressed = pt_dct_encode(&image, target_quality);
                }

                if (compressed.data && compressed.size > 0) {
                    if (use_flate) {
                        out_flate.assign(compressed.data, compressed.data + compressed.size);
                    } else {
                        out_jpeg.assign(compressed.data, compressed.data + compressed.size);
                        g_image_dct++;
                    }
                }
                pt_compressed_free(&compressed);
            }

            if (!out_jpeg.empty() || !out_flate.empty()) {
                size_t encoded_size = out_jpeg.empty() ? out_flate.size() : out_jpeg.size();
                if (encoded_size >= stream_len) {
                    fo.data = raw;
                } else {
                        remove_dict_entry(dict_str, "/Filter");
                        remove_dict_entry(dict_str, "/DecodeParms");
                        remove_dict_entry(dict_str, "/ColorSpace");
                        remove_dict_entry(dict_str, "/Length");
                        remove_dict_entry(dict_str, "/Decode");
                        remove_dict_entry(dict_str, "/BitsPerComponent");
                        remove_dict_entry(dict_str, "/Width");
                        remove_dict_entry(dict_str, "/Height");

                        std::string new_dict = dict_str;
                        size_t dd = new_dict.rfind(">>");
                        if (dd == std::string::npos) dd = new_dict.size();
                        
                        std::string filter = out_jpeg.empty() ? "/FlateDecode" : "/DCTDecode";
                        std::string bpc_str = is_binarized ? "1" : "8";
                        std::string cspace = (img_channels == 1) ? "/DeviceGray" : (img_channels == 4 ? "/DeviceCMYK" : "/DeviceRGB");
                        
                        std::string extra = "\n/Filter " + filter + "\n/BitsPerComponent " + bpc_str + "\n/ColorSpace " + cspace + "\n/Width " + std::to_string(new_w) + "\n/Height " + std::to_string(new_h) + "\n/Length " + std::to_string(encoded_size);
                        new_dict.insert(dd, extra);

                        std::string hdr = std::to_string(obj_id) + " " + std::to_string(gen) + " obj\n" + new_dict;
                        if (!hdr.empty() && hdr.back() != '\n' && hdr.back() != '\r') hdr += "\n";
                        hdr += "stream\n";
                        fo.data.assign(hdr.begin(), hdr.end());
                        const std::vector<uint8_t>& final_encoded = !out_flate.empty() ? out_flate : out_jpeg;
                        fo.data.insert(fo.data.end(), final_encoded.begin(), final_encoded.end());
                        const char *tail = "\nendstream\nendobj\n";
                        fo.data.insert(fo.data.end(), tail, tail + strlen(tail));
                        g_image_reencoded++;
                        return fo;
                    }
                } else {
                    fo.data = raw;
                    if (!skip_image) g_image_fallback++;
                }
            }
        }

    // Any image that cannot be safely decoded or made smaller is preserved.
    g_image_fallback++;
    fo.data = raw;
    return fo;
}

int rewrite_pdf_impl(pt_context *ctx, pt_state *st, const char *out_path) {
    if (!st->xref || st->xref->xref_store.empty()) return -1;

    g_image_objects = 0;
    g_image_eligible = 0;
    g_image_decoded = 0;
    g_image_reencoded = 0;
    g_image_fallback = 0;
    g_image_dct = 0;
    g_image_flate = 0;
    g_image_jpx = 0;
    g_image_ccitt = 0;
    g_image_other = 0;
    g_font_subset = 0;
    g_skip_mask = 0;
    g_skip_color = 0;
    g_skip_decode = 0;
    g_skip_filter = 0;

    struct Slot { int id, gen; long offset; };
    std::vector<Slot> slots;
    int compressed_entries = 0;

    for (size_t i = 0; i < st->xref->xref_store.size(); i++) {
        auto &x = st->xref->xref_store[i];
        if (x.stream_type_f1 == 2) compressed_entries++;
        bool is_direct = (x.stream_type_f1 == 1) || (x.status_obj == 'n' && x.stream_type_f1 != 2);
        if (is_direct && x.byte_offset > 0)
            slots.push_back({x.obj_id, x.gen_num, x.byte_offset});
    }
    std::sort(slots.begin(), slots.end(), [](const Slot &a, const Slot &b){ return a.offset < b.offset; });

    ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_END);
    long file_size = ctx->file->tell(ctx, ctx->file->f);

    g_font_codepoints.clear();
    for (size_t i = 0; i < slots.size(); ++i) {
        long end_hint = (i + 1 < slots.size()) ? slots[i + 1].offset : file_size;
        auto t_r_start = std::chrono::high_resolution_clock::now();
        auto raw = read_obj_raw(ctx, slots[i].offset, end_hint);
        auto t_r_end = std::chrono::high_resolution_clock::now();
        double tr = std::chrono::duration<double, std::milli>(t_r_end - t_r_start).count();
        if (tr > 5.0) {
        }
        if (!raw.empty()) collect_tounicode_codepoints(raw);
    }
    std::sort(g_font_codepoints.begin(), g_font_codepoints.end());
    g_font_codepoints.erase(std::unique(g_font_codepoints.begin(), g_font_codepoints.end()),
                            g_font_codepoints.end());

    std::map<int, Slot> slots_by_id;
    for (const Slot &slot : slots) slots_by_id[slot.id] = slot;

    // Follow the document graph from Catalog through Pages, Kids, Resources,
    // Contents and every indirect asset reference.
    std::set<int> reachable;
    std::vector<int> pending;
    if (st->xref->root_obj > 0 && slots_by_id.count(st->xref->root_obj)) {
        pending.push_back(st->xref->root_obj);
    }
    while (!pending.empty()) {
        int object_id = pending.back();
        pending.pop_back();
        if (!reachable.insert(object_id).second) continue;

        const Slot &slot = slots_by_id[object_id];
        long end_hint = file_size;
        auto next = std::upper_bound(slots.begin(), slots.end(), slot.offset,
            [](long offset, const Slot &candidate) { return offset < candidate.offset; });
        if (next != slots.end()) end_hint = next->offset;
        std::vector<uint8_t> object_raw = read_obj_raw(ctx, slot.offset, end_hint);
        std::string object_text(object_raw.begin(), object_raw.end());
        std::regex reference_pattern(R"((\d+)\s+(\d+)\s+R\b)");
        for (std::sregex_iterator it(object_text.begin(), object_text.end(), reference_pattern), end;
             it != end; ++it) {
            int referenced_id = std::stoi((*it)[1].str());
            if (slots_by_id.count(referenced_id) && !reachable.count(referenced_id)) {
                pending.push_back(referenced_id);
            }
        }
    }

    int n = (int)slots.size();
    printf("writer: found %d direct objects, %d reachable objects, %d compressed entries\n",
           n, (int)reachable.size(), compressed_entries);

    std::map<int, FlatObj> output_objs;
    int objstm_containers = 0;
    int extracted_objects = 0;


    double other_stream_time = 0;
    auto t_loop_start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < n; i++) {
        long end_hint = (i + 1 < n) ? slots[i+1].offset : file_size;
        auto t_r_start = std::chrono::high_resolution_clock::now();
        auto raw = read_obj_raw(ctx, slots[i].offset, end_hint);
        auto t_r_end = std::chrono::high_resolution_clock::now();
        double tr = std::chrono::duration<double, std::milli>(t_r_end - t_r_start).count();
        if (tr > 5.0) {
        }
        if (raw.empty()) continue;

        std::string s(raw.begin(), raw.end());
        bool has_stream = s.find("stream") != std::string::npos;
        int obj_id = slots[i].id;
        int gen_n  = slots[i].gen;
        // Parse actual N G obj header from raw bytes - xref obj_id wrong for linearized PDFs
        {
            size_t p = 0;
            while (p < s.size() && isspace((unsigned char)s[p])) p++;
            size_t ns = p;
            while (p < s.size() && isdigit((unsigned char)s[p])) p++;
            if (p > ns) {
                int fid = std::stoi(s.substr(ns, p-ns));
                while (p < s.size() && isspace((unsigned char)s[p])) p++;
                size_t gs = p;
                while (p < s.size() && isdigit((unsigned char)s[p])) p++;
                if (p > gs) {
                    int fg = std::stoi(s.substr(gs, p-gs));
                    while (p < s.size() && isspace((unsigned char)s[p])) p++;
                    if (p+3 <= s.size() && s.substr(p,3) == "obj") { obj_id = fid; gen_n = fg; }
                }
            }
        }

        if (has_stream) {
            std::string dict_str;

            const uint8_t *sd; size_t sl;
            bool parsed = parse_stream_bounds(raw, dict_str, sd, sl);

            if (parsed && (dict_has(dict_str, "/Type /ObjStm") || dict_has(dict_str, "/Type/ObjStm"))) {
                objstm_containers++;
                auto t_e_start = std::chrono::high_resolution_clock::now();
                auto embedded = extract_objstm(raw);
                auto t_e_end = std::chrono::high_resolution_clock::now();
                double te = std::chrono::duration<double, std::milli>(t_e_end - t_e_start).count();
                if (te > 5.0) {
                }
                extracted_objects += (int)embedded.size();
                for (auto &ep : embedded) {
                    int eid = ep.first;
                    std::string ebody = ep.second;
                    if (output_objs.find(eid) != output_objs.end()) continue;
                    FlatObj efo;
                    efo.id  = eid;
                    efo.gen = 0;
                    std::string hdr2 = std::to_string(eid) + " 0 obj\n" + ebody + "\nendobj\n";
                    efo.data.assign(hdr2.begin(), hdr2.end());
                    output_objs[eid] = std::move(efo);
                }
                continue; // ObjStm container not written
            }

            FlatObj fo;
            try {
                fo = process_stream_obj(ctx, st, obj_id, gen_n, raw);
            } catch (const std::exception& e) {
                printf("EXCEPTION processing object %d: %s\n", obj_id, e.what());
                fflush(stdout);
                break;
            } catch (...) {
                printf("UNKNOWN EXCEPTION processing object %d\n", obj_id);
                fflush(stdout);
                break;
            }


            if (!fo.data.empty()) {
                output_objs[obj_id] = std::move(fo);
            }
        } else {
            auto t_o_start = std::chrono::high_resolution_clock::now();
            FlatObj fo;
            fo.id  = obj_id;
            fo.gen = gen_n;
            // Disable linearization
            size_t lin = s.find("/Linearized");
            if (lin != std::string::npos) s.replace(lin, 11, "/Ignored___");
            size_t real_end = s.find("endobj");
            if (real_end != std::string::npos) s.resize(real_end + 6);
            if (s.find("endobj") == std::string::npos) {
                if (!s.empty() && s.back() != '\n' && s.back() != '\r') s += "\n";
                s += "endobj\n";
            } else if (!s.empty() && s.back() != '\n') {
                s += "\n";
            }
            fo.data.assign(s.begin(), s.end());
            output_objs[obj_id] = std::move(fo);
            auto t_o_end = std::chrono::high_resolution_clock::now();
            other_stream_time += std::chrono::duration<double, std::milli>(t_o_end - t_o_start).count();
        }
    }

                        
    printf("writer: ObjStm containers: %d, extracted objects: %d\n", objstm_containers, extracted_objects);
    auto t_loop_end = std::chrono::high_resolution_clock::now();
        printf("writer: images: objects=%d eligible=%d decoded=%d reencoded=%d fallback=%d\n",
            g_image_objects, g_image_eligible, g_image_decoded,
            g_image_reencoded, g_image_fallback);
            printf("writer: image filters: DCT=%d Flate=%d JPX=%d CCITT=%d other=%d\n",
                g_image_dct, g_image_flate, g_image_jpx, g_image_ccitt, g_image_other);
    printf("writer: image skips: mask=%d color=%d decode=%d filter=%d\n",
        g_skip_mask, g_skip_color, g_skip_decode, g_skip_filter);
    printf("writer: font subsets: %d\n", g_font_subset);
    printf("writer: total output objects: %zu\n", output_objs.size());

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        printf("writer: ERROR: failed to open output file %s\n", out_path);
        return -1;
    }

    const char *pdf_header = "%PDF-1.5\n%\xE2\xE3\xCF\xD3\n";
    fwrite(pdf_header, 1, strlen(pdf_header), out);

    int max_id = 0;
    for (auto &kv : output_objs) if (kv.first > max_id) max_id = kv.first;
    max_id += 2;

    std::vector<long> xref_offsets(max_id + 1, 0);
    std::vector<int>  xref_gens(max_id + 1, 0);
    std::vector<bool> xref_used(max_id + 1, false);
    xref_gens[0] = 65535;

    for (auto &kv : output_objs) {
        FlatObj &fo = kv.second;
        if (fo.data.empty() || fo.id <= 0 || fo.id >= max_id) continue;
        xref_offsets[fo.id] = (long)ftell(out);
        xref_gens[fo.id]    = fo.gen;
        xref_used[fo.id]    = true;
        fwrite(fo.data.data(), 1, fo.data.size(), out);
    }

    int root_obj = st->xref->root_obj;
    int root_gen = st->xref->root_gen_num;
    if (root_obj <= 0) {
        for (auto &kv : output_objs) {
            FlatObj &fo = kv.second;
            std::string ss(fo.data.begin(), fo.data.begin() + std::min((size_t)512, fo.data.size()));
            if (dict_has(ss, "/Type/Catalog") || dict_has(ss, "/Type /Catalog")) {
                root_obj = fo.id;
                root_gen = fo.gen;
                break;
            }
        }
    }

    long xref_start = (long)ftell(out);
    fprintf(out, "xref\n");

    fprintf(out, "0 %d\n", max_id);
    fprintf(out, "0000000000 65535 f \n");
    for (int i = 1; i < max_id; i++) {
        if (xref_used[i])
            fprintf(out, "%010ld %05d n \n", xref_offsets[i], xref_gens[i]);
        else
            fprintf(out, "0000000000 00000 f \n");
    }

    if (root_obj > 0)
        fprintf(out, "trailer\n<</Size %d/Root %d %d R>>\nstartxref\n%ld\n%%%%EOF\n",
                max_id, root_obj, root_gen, xref_start);
    fclose(out);

    printf("writer: done -- output: %s\n", out_path);
    return 0;
}

void main_writer(pt_state *st) {
    if (st && st->writer) st->writer->rewrite = rewrite_pdf_impl;
}

static void collect_tounicode_codepoints(const std::vector<uint8_t> &raw) {
    std::string dict_str = extract_dict(raw);
    if (dict_str.find("/ToUnicode") == std::string::npos) return;
    const uint8_t *stream_data = nullptr;
    size_t stream_len = 0;
    if (!extract_stream(raw, &stream_data, &stream_len)) return;
    std::vector<uint8_t> decoded = dict_str.find("/FlateDecode") != std::string::npos
        ? flate_decompress(stream_data, stream_len)
        : std::vector<uint8_t>(stream_data, stream_data + stream_len);
    if (decoded.empty()) return;
    std::string cmap(decoded.begin(), decoded.end());
    std::regex unicode_pair(R"(<[0-9A-Fa-f]+>\s+<([0-9A-Fa-f]+)>)");
    for (std::sregex_iterator it(cmap.begin(), cmap.end(), unicode_pair), end; it != end; ++it) {
        try {
            uint32_t codepoint = (uint32_t)std::stoul((*it)[1].str(), nullptr, 16);
            if (codepoint <= 0x10FFFF) g_font_codepoints.push_back(codepoint);
        } catch (...) {
        }
    }
}

static std::vector<uint8_t> parse_palette(pt_context *ctx, pt_state *st, int cs_id) {
    std::vector<uint8_t> pal;
    long offset = st->xref->look(ctx, st, cs_id);
    if (offset <= 0) return pal;
    
    long old_pos = ctx->file->tell(ctx, ctx->file->f);
    ctx->file->seek(ctx, ctx->file->f, offset, PT_SEEK_SET);
    
    std::vector<char> buf(65536, 0);
    size_t n = ctx->file->read(ctx, buf.data(), 1, 65535, ctx->file->f);
    buf[n] = 0;
    std::string s(buf.data(), n);
    
    size_t idx = s.find("/Indexed");
    if (idx == std::string::npos) {
        ctx->file->seek(ctx, ctx->file->f, old_pos, PT_SEEK_SET);
        return pal;
    }
    
    size_t end_bracket = s.find("]", idx);
    if (end_bracket == std::string::npos) end_bracket = s.size();
    
    size_t hex_start = s.find("<", idx);
    size_t lit_start = s.find("(", idx);
    size_t ref_pos = s.find(" R", idx);
    
    bool is_hex = (hex_start != std::string::npos && hex_start < end_bracket);
    bool is_lit = (lit_start != std::string::npos && lit_start < end_bracket);
    bool is_ref = (ref_pos != std::string::npos && ref_pos < end_bracket);
    
    if (is_hex && (!is_lit || hex_start < lit_start) && (!is_ref || hex_start < ref_pos)) {
        size_t hex_end = s.find(">", hex_start);
        if (hex_end != std::string::npos) {
            std::string hex = s.substr(hex_start+1, hex_end - hex_start - 1);
            for (size_t i=0; i<hex.size(); i+=2) {
                while(i < hex.size() && isspace(hex[i])) i++;
                if (i+1 < hex.size()) {
                    std::string byte_str = hex.substr(i, 2);
                    pal.push_back((uint8_t)strtol(byte_str.c_str(), nullptr, 16));
                }
            }
        }
    } else if (is_lit && (!is_ref || lit_start < ref_pos)) {
        size_t p = lit_start + 1;
        while(p < end_bracket && s[p] != ')') {
            if (s[p] == '\\') {
                p++;
                if (p < end_bracket && isdigit(s[p])) {
                    int val = 0;
                    for (int k=0; k<3 && p < end_bracket && isdigit(s[p]); k++, p++) {
                        val = val * 8 + (s[p] - '0');
                    }
                    pal.push_back((uint8_t)val);
                    continue;
                } else if (p < end_bracket) {
                    if (s[p] == 'n') pal.push_back('\n');
                    else if (s[p] == 'r') pal.push_back('\r');
                    else if (s[p] == 't') pal.push_back('\t');
                    else if (s[p] == 'b') pal.push_back('\b');
                    else if (s[p] == 'f') pal.push_back('\f');
                    else if (s[p] == '(') pal.push_back('(');
                    else if (s[p] == ')') pal.push_back(')');
                    else if (s[p] == '\\') pal.push_back('\\');
                    p++;
                    continue;
                }
            } else {
                pal.push_back((uint8_t)s[p]);
                p++;
            }
        }
    } else if (is_ref) {
        int ref_id = -1;
        size_t p = ref_pos;
        while(p > idx && isspace(s[p-1])) p--;
        while(p > idx && isdigit(s[p-1])) p--;
        if (s[p-1] == ' ' && isdigit(s[p-2])) {
            p--;
            while(p > idx && isdigit(s[p-1])) p--;
            ref_id = std::stoi(s.substr(p));
        }
        if (ref_id > 0) {
            long ref_offset = st->xref->look(ctx, st, ref_id);
            if (ref_offset > 0) {
                ctx->file->seek(ctx, ctx->file->f, ref_offset, PT_SEEK_SET);
                std::vector<char> ref_buf(65536, 0);
                size_t rn = ctx->file->read(ctx, ref_buf.data(), 1, 65535, ctx->file->f);
                std::string rs(ref_buf.data(), rn);
                size_t stream_pos = rs.find("stream");
                size_t endstream_pos = rs.find("endstream");
                if (stream_pos != std::string::npos && endstream_pos != std::string::npos) {
                    stream_pos += 6;
                    while(stream_pos < rs.size() && (rs[stream_pos] == '\r' || rs[stream_pos] == '\n')) stream_pos++;
                    const unsigned char *src = (const unsigned char*)rs.data() + stream_pos;
                    size_t src_len = endstream_pos - stream_pos;
                    if (rs.find("FlateDecode") != std::string::npos) {
                        pal = flate_decompress(src, src_len);
                    } else {
                        pal.assign(src, src + src_len);
                    }
                }
            }
        }
    }
    
    ctx->file->seek(ctx, ctx->file->f, old_pos, PT_SEEK_SET);
    return pal;
}

static std::string resolve_image_colorspace(pt_context *ctx, pt_state *st,
                                            const std::string &dict_str) {
    size_t cs_pos = dict_str.find("/ColorSpace");
    if (cs_pos == std::string::npos) return {};
    std::regex ref_pattern(R"((\d+)\s+(\d+)\s+R)");
    std::smatch match;
    std::string sub = dict_str.substr(cs_pos, std::min(dict_str.size() - cs_pos, (size_t)100));
    if (!std::regex_search(sub, match, ref_pattern)) return {};
    int object_id = std::stoi(match[1].str());
    long offset = st->xref->look(ctx, st, object_id);
    if (offset <= 0) return {};
    std::vector<uint8_t> raw = read_obj_raw(ctx, offset, offset + 100000);
    std::string s(raw.begin(), raw.end());
    size_t end_pos = s.find("endobj");
    if (end_pos != std::string::npos) s = s.substr(0, end_pos);
    return s;
}