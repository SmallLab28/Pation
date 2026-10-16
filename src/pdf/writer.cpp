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
#include "pdf/page.h"
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
#include <unordered_map>
#include <unordered_set>
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

#include <openjpeg.h>

struct MemStreamBuffer {
    const uint8_t *data;
    size_t size;
    size_t offset;
};

static OPJ_SIZE_T opj_mem_read(void *p_buffer, OPJ_SIZE_T p_nb_bytes, void *p_user_data) {
    MemStreamBuffer *ms = (MemStreamBuffer*)p_user_data;
    if (!ms || ms->offset >= ms->size) return (OPJ_SIZE_T)-1;
    size_t to_read = std::min((size_t)p_nb_bytes, ms->size - ms->offset);
    memcpy(p_buffer, ms->data + ms->offset, to_read);
    ms->offset += to_read;
    return to_read;
}

static OPJ_OFF_T opj_mem_skip(OPJ_OFF_T p_nb_bytes, void *p_user_data) {
    MemStreamBuffer *ms = (MemStreamBuffer*)p_user_data;
    if (!ms) return (OPJ_OFF_T)-1;
    if (p_nb_bytes < 0) {
        if (ms->offset < (size_t)(-p_nb_bytes)) {
            ms->offset = 0;
            return (OPJ_OFF_T)-1;
        }
        ms->offset -= (size_t)(-p_nb_bytes);
        return p_nb_bytes;
    }
    size_t to_skip = std::min((size_t)p_nb_bytes, ms->size - ms->offset);
    ms->offset += to_skip;
    return (OPJ_OFF_T)to_skip;
}

static OPJ_BOOL opj_mem_seek(OPJ_OFF_T p_nb_bytes, void *p_user_data) {
    MemStreamBuffer *ms = (MemStreamBuffer*)p_user_data;
    if (!ms || p_nb_bytes < 0 || (size_t)p_nb_bytes > ms->size) return OPJ_FALSE;
    ms->offset = (size_t)p_nb_bytes;
    return OPJ_TRUE;
}

static void opj_quiet_callback(const char *msg, void *client_data) {
    (void)msg; (void)client_data;
}

static bool pt_jpx_decode(const uint8_t *stream_data, size_t stream_len,
                          std::vector<uint8_t> &out_pixels,
                          int &out_w, int &out_h, int &out_channels) {
    if (!stream_data || stream_len < 4) return false;

    OPJ_CODEC_FORMAT fmt = OPJ_CODEC_J2K;
    if (stream_len >= 12 &&
        stream_data[4] == 'j' && stream_data[5] == 'P' &&
        stream_data[6] == ' ' && stream_data[7] == ' ') {
        fmt = OPJ_CODEC_JP2;
    } else if (stream_len >= 2 && stream_data[0] == 0xFF && stream_data[1] == 0x4F) {
        fmt = OPJ_CODEC_J2K;
    }

    opj_codec_t *codec = opj_create_decompress(fmt);
    if (!codec) return false;

    opj_set_info_handler(codec, opj_quiet_callback, NULL);
    opj_set_warning_handler(codec, opj_quiet_callback, NULL);
    opj_set_error_handler(codec, opj_quiet_callback, NULL);

    opj_dparameters_t params;
    opj_set_default_decoder_parameters(&params);
    if (!opj_setup_decoder(codec, &params)) {
        opj_destroy_codec(codec);
        return false;
    }

    MemStreamBuffer ms = { stream_data, stream_len, 0 };
    opj_stream_t *stream = opj_stream_create(1024, OPJ_TRUE);
    if (!stream) {
        opj_destroy_codec(codec);
        return false;
    }

    opj_stream_set_user_data(stream, &ms, NULL);
    opj_stream_set_user_data_length(stream, stream_len);
    opj_stream_set_read_function(stream, opj_mem_read);
    opj_stream_set_skip_function(stream, opj_mem_skip);
    opj_stream_set_seek_function(stream, opj_mem_seek);

    opj_image_t *image = NULL;
    if (!opj_read_header(stream, codec, &image) || !image) {
        opj_stream_destroy(stream);
        opj_destroy_codec(codec);
        return false;
    }

    if (!opj_decode(codec, stream, image) || !opj_end_decompress(codec, stream)) {
        opj_image_destroy(image);
        opj_stream_destroy(stream);
        opj_destroy_codec(codec);
        return false;
    }

    int w = image->x1 - image->x0;
    int h = image->y1 - image->y0;
    int num_comps = image->numcomps;

    if (w <= 0 || h <= 0 || (num_comps != 1 && num_comps != 3 && num_comps != 4)) {
        opj_image_destroy(image);
        opj_stream_destroy(stream);
        opj_destroy_codec(codec);
        return false;
    }

    out_w = w;
    out_h = h;
    out_channels = num_comps;
    out_pixels.resize((size_t)w * h * num_comps);

    for (int c = 0; c < num_comps; c++) {
        const OPJ_INT32 *comp_data = image->comps[c].data;
        int prec = image->comps[c].prec;
        if (!comp_data) {
            opj_image_destroy(image);
            opj_stream_destroy(stream);
            opj_destroy_codec(codec);
            return false;
        }
        for (int i = 0; i < w * h; i++) {
            int val = comp_data[i];
            if (prec > 8) val >>= (prec - 8);
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            out_pixels[i * num_comps + c] = (uint8_t)val;
        }
    }

    opj_image_destroy(image);
    opj_stream_destroy(stream);
    opj_destroy_codec(codec);
    return true;
}

struct SMaskTarget {
    long target_w;
    long target_h;
};
static std::unordered_map<int, SMaskTarget> s_smask_targets;
static std::unordered_set<int> s_known_smasks;

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
static bool s_used_codepoints[65536];
static std::unordered_set<int> s_dropped_ids;

static inline void add_font_codepoint(uint32_t c) {
    if (c < 65536 && !s_used_codepoints[c]) {
        s_used_codepoints[c] = true;
        g_font_codepoints.push_back(c);
    }
}

static inline int safe_stoi(const std::string &s, int def = 0) {
    if (s.empty()) return def;
    char *end = nullptr;
    long v = strtol(s.c_str(), &end, 10);
    return (end && end != s.c_str()) ? (int)v : def;
}

static inline long safe_stol(const std::string &s, long def = 0) {
    if (s.empty()) return def;
    char *end = nullptr;
    long v = strtol(s.c_str(), &end, 10);
    return (end && end != s.c_str()) ? v : def;
}

static inline bool parse_hex_u32(const std::string &s, uint32_t &val) {
    if (s.empty()) return false;
    char *end = nullptr;
    unsigned long v = strtoul(s.c_str(), &end, 16);
    if (!end || *end != '\0') return false;
    val = (uint32_t)v;
    return true;
}

static void collect_tounicode_codepoints(const std::vector<uint8_t> &raw);
static void collect_used_codes_from_content(const std::vector<uint8_t> &raw);
static std::vector<uint8_t> read_obj_raw(pt_context *ctx, long offset, long end_hint);
static std::string resolve_image_colorspace(pt_context *ctx, pt_state *st,
                                            const std::string &dict_str);
static std::vector<uint8_t> parse_palette(pt_context *ctx, pt_state *st, int cs_id);
static std::unordered_map<int, std::string> s_cs_cache;
static std::unordered_map<int, std::vector<uint8_t>> s_pal_cache;
static std::unordered_map<int, bool> s_devicen_cyk_cache;



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

static std::vector<uint8_t> flate_compress(const uint8_t *data, size_t len, int level = 6) {
    if (!data || len == 0) return {};
    size_t out_len = 0;
    mz_uint comp_flags = TDEFL_COMPUTE_ADLER32 | tdefl_create_comp_flags_from_zip_params(level, 15, MZ_DEFAULT_STRATEGY);
    void *out = tdefl_compress_mem_to_heap(data, len, &out_len, comp_flags);
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

static void collect_ref_ids_for_key(const std::string &dict_str, const std::string &key, std::unordered_set<int> &ids) {
    size_t search_pos = 0;
    while (true) {
        size_t pos = dict_str.find(key, search_pos);
        if (pos == std::string::npos) break;
        if (pos + key.size() < dict_str.size() && isalnum((unsigned char)dict_str[pos + key.size()])) {
            search_pos = pos + key.size();
            continue;
        }
        size_t p = pos + key.size();
        while (p < dict_str.size() && isspace((unsigned char)dict_str[p])) p++;
        size_t num_start = p;
        while (p < dict_str.size() && isdigit((unsigned char)dict_str[p])) p++;
        if (p > num_start) {
            int ref_id = safe_stoi(dict_str.substr(num_start, p - num_start));
            while (p < dict_str.size() && isspace((unsigned char)dict_str[p])) p++;
            while (p < dict_str.size() && isdigit((unsigned char)dict_str[p])) p++;
            while (p < dict_str.size() && isspace((unsigned char)dict_str[p])) p++;
            if (p < dict_str.size() && dict_str[p] == 'R') {
                ids.insert(ref_id);
            }
        }
        search_pos = pos + key.size();
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

static FlatObj sanitize_raw_stream_dict(int obj_id, int gen, const std::vector<uint8_t> &raw) {
    FlatObj fo;
    fo.id = obj_id;
    fo.obj_id = obj_id;
    fo.gen = gen;
    std::string dict_str;
    const uint8_t *sd = nullptr;
    size_t sl = 0;
    if (parse_stream_bounds(raw, dict_str, sd, sl)) {
        bool changed = false;
        while (dict_has(dict_str, "/Metadata")) { remove_dict_entry(dict_str, "/Metadata"); changed = true; }
        while (dict_has(dict_str, "/PieceInfo")) { remove_dict_entry(dict_str, "/PieceInfo"); changed = true; }
        while (dict_has(dict_str, "/Thumb")) { remove_dict_entry(dict_str, "/Thumb"); changed = true; }
        if (changed) {
            std::string hdr = std::to_string(obj_id) + " " + std::to_string(gen) + " obj\n" + dict_str;
            if (!hdr.empty() && hdr.back() != '\n' && hdr.back() != '\r') hdr += "\n";
            hdr += "stream\n";
            fo.data.assign(hdr.begin(), hdr.end());
            if (sd && sl > 0) {
                fo.data.insert(fo.data.end(), sd, sd + sl);
            }
            const char *tail = "\nendstream\nendobj\n";
            fo.data.insert(fo.data.end(), tail, tail + strlen(tail));
            return fo;
        }
    }
    fo.data = raw;
    return fo;
}

FlatObj process_stream_obj(pt_context *ctx, pt_state *st, int obj_id, int gen, const std::vector<uint8_t>& raw) {
    FlatObj fo;
    fo.id = obj_id;
    fo.obj_id = obj_id;
    fo.gen = gen;
    std::string dict_str = extract_dict(raw);

    if (s_dropped_ids.count(obj_id) || dict_has(dict_str, "/Type /Metadata") || dict_has(dict_str, "/Type/Metadata")) {
        return fo;
    }
    while (dict_has(dict_str, "/Metadata")) remove_dict_entry(dict_str, "/Metadata");
    while (dict_has(dict_str, "/PieceInfo")) remove_dict_entry(dict_str, "/PieceInfo");
    while (dict_has(dict_str, "/Thumb")) remove_dict_entry(dict_str, "/Thumb");

    const uint8_t *stream_data = nullptr;
    size_t stream_len = 0;
    bool ext_res = extract_stream(raw, &stream_data, &stream_len);
    if (!ext_res) {
        fo = sanitize_raw_stream_dict(obj_id, gen, raw);
        return fo;
    }

    bool is_image = dict_str.find("/Subtype /Image") != std::string::npos ||
                    dict_str.find("/Subtype/Image") != std::string::npos;
    if (!is_image) {
        bool is_font_program = dict_str.find("/Length1") != std::string::npos ||
                               dict_str.find("/FontFile") != std::string::npos ||
                               dict_str.find("/Type1C") != std::string::npos ||
                               dict_str.find("/CIDFontType0C") != std::string::npos;
        if (is_font_program && !g_font_codepoints.empty()) {
            std::vector<uint8_t> font_bytes = dict_str.find("/FlateDecode") != std::string::npos
                ? flate_decompress(stream_data, stream_len)
                : std::vector<uint8_t>(stream_data, stream_data + stream_len);

            bool is_truetype = font_bytes.size() >= 4 &&
                               ((font_bytes[0] == 0 && font_bytes[1] == 1 &&
                                 font_bytes[2] == 0 && font_bytes[3] == 0) ||
                                (font_bytes[0] == 't' && font_bytes[1] == 'r' &&
                                 font_bytes[2] == 'u' && font_bytes[3] == 'e'));
            bool is_cff = font_bytes.size() >= 4 && (font_bytes[0] == 1 && font_bytes[1] == 0);

            uint8_t *subset = NULL;
            size_t subset_size = 0;

            if (is_truetype) {
                pt_font font = {};
                if (pt_font_open(&font, font_bytes.data(), font_bytes.size()) == 0) {
                    pt_glyph_set glyphs = pt_glyph_set_create(font.num_glyphs);
                    pt_cmap_mark(&font, &glyphs, g_font_codepoints.data(), g_font_codepoints.size());
                    pt_glyf_closure(&font, &glyphs);
                    subset = pt_font_subset(&font, &glyphs, &subset_size);
                    pt_glyph_set_free(&glyphs);
                    pt_font_close(&font);
                }
            } else if (is_cff) {
                pt_glyph_set glyphs = pt_glyph_set_create(65535);
                for (uint32_t cp : g_font_codepoints) {
                    if (cp < glyphs.cap) glyphs.used[cp] = true;
                }
                subset = pt_cff_subset(font_bytes.data(), font_bytes.size(), &glyphs, &subset_size);
                pt_glyph_set_free(&glyphs);
            }

            if (subset && subset_size > 0 && subset_size < font_bytes.size()) {
                std::vector<uint8_t> uncompressed(subset, subset + subset_size);
                std::free(subset);
                std::vector<uint8_t> compressed = flate_encode(uncompressed);
                const std::vector<uint8_t> &final_font = (!compressed.empty() && compressed.size() < uncompressed.size())
                    ? compressed : uncompressed;
                bool is_compressed = (!compressed.empty() && compressed.size() < uncompressed.size());

                remove_dict_entry(dict_str, "/Filter");
                remove_dict_entry(dict_str, "/DecodeParms");
                remove_dict_entry(dict_str, "/Length");
                remove_dict_entry(dict_str, "/Length1");
                while (dict_has(dict_str, "/Metadata")) remove_dict_entry(dict_str, "/Metadata");
                while (dict_has(dict_str, "/PieceInfo")) remove_dict_entry(dict_str, "/PieceInfo");
                while (dict_has(dict_str, "/Thumb")) remove_dict_entry(dict_str, "/Thumb");

                size_t dd = dict_str.rfind(">>");
                if (dd == std::string::npos) dd = dict_str.size();
                std::string extra = "\n/Length " + std::to_string(final_font.size());
                if (is_truetype) extra += "\n/Length1 " + std::to_string(subset_size);
                if (is_compressed) extra += "\n/Filter /FlateDecode";
                extra += "\n";
                dict_str.insert(dd, extra);

                std::string header = std::to_string(obj_id) + " " + std::to_string(gen) + " obj\n" + dict_str;
                if (!header.empty() && header.back() != '\n' && header.back() != '\r') header += "\n";
                header += "stream\n";
                fo.data.assign(header.begin(), header.end());
                fo.data.insert(fo.data.end(), final_font.begin(), final_font.end());
                const char *tail = "\nendstream\nendobj\n";
                fo.data.insert(fo.data.end(), tail, tail + strlen(tail));
                g_font_subset++;
                return fo;
            }
            if (subset) std::free(subset);
        }
        if (dict_str.find("/Filter") == std::string::npos && stream_len > 64) {
            auto t_encode_start = std::chrono::high_resolution_clock::now();
            std::vector<uint8_t> uncompressed(stream_data, stream_data + stream_len);
            std::vector<uint8_t> compressed = flate_encode(uncompressed);
            auto t_encode_end = std::chrono::high_resolution_clock::now();
            if (!compressed.empty() && compressed.size() < uncompressed.size()) {
                remove_dict_entry(dict_str, "/Length");
                while (dict_has(dict_str, "/Metadata")) remove_dict_entry(dict_str, "/Metadata");
                while (dict_has(dict_str, "/PieceInfo")) remove_dict_entry(dict_str, "/PieceInfo");
                while (dict_has(dict_str, "/Thumb")) remove_dict_entry(dict_str, "/Thumb");
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
        fo = sanitize_raw_stream_dict(obj_id, gen, raw);
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
            bool was_jpx = (dict_str.find("/JPXDecode") != std::string::npos);
            long bpc = dict_int(dict_str, "/BitsPerComponent", 8);
            bool is_image_mask = (dict_str.find("/ImageMask true") != std::string::npos);
            bool has_explicit_mask = (dict_str.find("/Mask") != std::string::npos &&
                                      dict_str.find("/SMask") == std::string::npos);
            bool is_smask_obj = (s_known_smasks.count(obj_id) > 0);
            bool supported_filter = was_dct || was_flate || was_jpx;
            bool supported_depth = bpc == 1 || bpc == 8;
            std::string real_cs = dict_str;
            int cs_id = -1;
            if (dict_str.find("/ColorSpace") != std::string::npos) {
                size_t cs_pos = dict_str.find("/ColorSpace");
                std::regex ref_pattern(R"((\d+)\s+(\d+)\s+R)");
                std::smatch match;
                std::string sub = dict_str.substr(cs_pos, std::min(dict_str.size() - cs_pos, (size_t)100));
                if (std::regex_search(sub, match, ref_pattern)) cs_id = safe_stoi(match[1].str());
                std::string resolved = resolve_image_colorspace(ctx, st, dict_str);
                if (!resolved.empty()) real_cs = resolved;
            }
            bool is_indexed = (real_cs.find("/Indexed") != std::string::npos);
            bool is_devicen_cyk = false;
            if (is_indexed) {
                if (real_cs.find("Cyan") != std::string::npos && real_cs.find("Yellow") != std::string::npos) {
                    is_devicen_cyk = true;
                } else if (cs_id > 0) {
                    auto it = s_devicen_cyk_cache.find(cs_id);
                    if (it != s_devicen_cyk_cache.end()) {
                        is_devicen_cyk = it->second;
                    } else {
                        std::regex base_ref_pattern(R"(/Indexed\s+(\d+)\s+(\d+)\s+R)");
                        std::smatch base_match;
                        if (std::regex_search(real_cs, base_match, base_ref_pattern)) {
                            int base_id = safe_stoi(base_match[1].str());
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
                        s_devicen_cyk_cache[cs_id] = is_devicen_cyk;
                    }
                }
            }
            bool indexed_or_special = real_cs.find("/Separation") != std::string::npos ||
                                      (real_cs.find("/DeviceN") != std::string::npos && !is_devicen_cyk);
            bool is_cmyk = (real_cs.find("/DeviceCMYK") != std::string::npos);
            bool skip_image = is_image_mask || has_explicit_mask || !supported_filter || !supported_depth || indexed_or_special || bpc == 1;
            if (skip_image) {
                if (is_image_mask || has_explicit_mask) g_skip_mask++;
                if (dict_str.find("/Indexed") != std::string::npos ||
                    dict_str.find("/Separation") != std::string::npos ||
                    dict_str.find("/DeviceN") != std::string::npos ||
                    dict_str.find("/ICCBased") != std::string::npos) g_skip_color++;
                if (dict_str.find("/Decode") != std::string::npos ||
                    dict_str.find("/Intent") != std::string::npos) g_skip_decode++;
                if (!supported_filter) g_skip_filter++;
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
            } else if (was_jpx) {
                int out_w = 0, out_h = 0, out_channels = 0;
                if (!pt_jpx_decode(stream_data, stream_len, raw_pixels, out_w, out_h, out_channels)) {
                    skip_image = true;
                    goto raw_fallback;
                }
                img_w = out_w;
                img_h = out_h;
                img_channels = out_channels;
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
                            if (pal.empty()) {
                                pt_img_free(&decoded);
                                skip_image = true;
                                goto raw_fallback;
                            }
                            std::vector<uint8_t> rgb_pal;
                            size_t num_colors = pal.size() / 3;
                            if (is_devicen_cyk && pal.size() >= 3) {
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
                            const uint8_t *use_pal = (is_devicen_cyk && pal.size() >= 3) ? rgb_pal.data() : pal.data();
                            size_t pal_size = (is_devicen_cyk && pal.size() >= 3) ? rgb_pal.size() : pal.size();

                            if (pal_size >= 3) {
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
                            } else {
                                uint8_t *gray = (uint8_t*)malloc((size_t)decoded.width * decoded.height);
                                for (long i = 0; i < (long)decoded.width * decoded.height; i++) {
                                    uint8_t idx = decoded.pixels[i];
                                    gray[i] = (idx < pal_size) ? use_pal[idx] : idx;
                                }
                                free(decoded.pixels);
                                decoded.pixels = gray;
                                decoded.components = 1;
                                decoded.cs = PT_CS_GRAY;
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

            bool is_grayscale = true;
            if (img_channels == 4 || is_devicen_cyk) is_grayscale = false;
            else if (img_channels >= 3 && !raw_pixels.empty()) {
                size_t color_pixels = 0;
                size_t total_pixels = (size_t)w * h;
                size_t color_threshold = std::max((size_t)800, (size_t)(total_pixels * 0.005));
                for (size_t i = 0; i < raw_pixels.size(); i += img_channels) {
                    int r = raw_pixels[i];
                    int g = raw_pixels[i+1];
                    int b = raw_pixels[i+2];
                    if (abs(r - g) > 15 || abs(r - b) > 15 || abs(g - b) > 15) {
                        color_pixels++;
                        if (color_pixels > color_threshold) { is_grayscale = false; break; }
                    }
                }
            }

            if (is_grayscale && img_channels >= 3 && !raw_pixels.empty()) {
                std::vector<uint8_t> gray_pixels(w * h);
                for (long i = 0; i < w * h; i++) {
                    int r = raw_pixels[i * img_channels + 0];
                    int g = raw_pixels[i * img_channels + 1];
                    int b = raw_pixels[i * img_channels + 2];
                    gray_pixels[i] = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
                }
                raw_pixels = std::move(gray_pixels);
                img_channels = 1;
            }

            std::vector<uint8_t> out_jpeg;
            std::vector<uint8_t> out_flate;
            long new_w = w, new_h = h;
            
            if (!skip_image && !raw_pixels.empty()) {
                if (is_smask_obj && s_smask_targets.find(obj_id) != s_smask_targets.end()) {
                    const auto &target = s_smask_targets[obj_id];
                    new_w = target.target_w;
                    new_h = target.target_h;
                } else {
                    int target_dpi = (st->writer && st->writer->target_dpi > 0) ? st->writer->target_dpi : 150;
                    if (target_dpi < 20) target_dpi = 20; 

                    int target_max_dim = (int)(target_dpi * 11.7);
                    if (target_max_dim < 250) target_max_dim = 250;
                    int max_dim = std::max(w, h);
                    
                    double scale = (max_dim > target_max_dim && max_dim > 300) ? (double)target_max_dim / max_dim : 1.0;
                    
                    if (scale < 1.0 && (was_dct || was_flate || was_jpx || is_devicen_cyk)) {
                        new_w = (long)(w * scale);
                        new_h = (long)(h * scale);
                    }
                
                    if (new_w < 16) new_w = std::min((long)w, 16L);
                    if (new_h < 16) new_h = std::min((long)h, 16L);
                }

                stbir_pixel_layout resize_layout = (img_channels == 1) ? STBIR_1CHANNEL :
                                                   (img_channels == 3) ? STBIR_RGB :
                                                   (img_channels == 4) ? STBIR_4CHANNEL : STBIR_1CHANNEL;

                std::vector<uint8_t> scaled_pixels;
                if (new_w != w || new_h != h) {
                    scaled_pixels.resize(new_w * new_h * img_channels);
                    stbir_resize_uint8_srgb(raw_pixels.data(), (int)w, (int)h, 0, scaled_pixels.data(), (int)new_w, (int)new_h, 0, resize_layout);
                } else {
                    scaled_pixels = raw_pixels;
                }

                if (is_smask_obj) {
                    size_t deflated_size = 0;
                    void *deflated = tdefl_compress_mem_to_heap(
                        scaled_pixels.data(), scaled_pixels.size(), &deflated_size,
                        TDEFL_DEFAULT_MAX_PROBES);
                    if (deflated && deflated_size > 0) {
                        out_flate.assign((uint8_t*)deflated, (uint8_t*)deflated + deflated_size);
                        mz_free(deflated);
                    }
                } else {
                    pt_image image = {};
                    image.pixels = scaled_pixels.data();
                    image.width = (int)new_w;
                    image.height = (int)new_h;
                    image.components = img_channels;
                    image.cs = img_channels == 1 ? PT_CS_GRAY :
                               img_channels == 4 ? PT_CS_CMYK : PT_CS_RGB;
                               
                    int target_quality = (st && st->writer && st->writer->target_quality > 0) ? st->writer->target_quality : 25;
                    if (target_quality < 5) target_quality = 5;
                    pt_compressed compressed = pt_dct_encode(&image, target_quality);

                    if (compressed.data && compressed.size > 0) {
                        out_jpeg.assign(compressed.data, compressed.data + compressed.size);
                        g_image_dct++;
                    }
                    pt_compressed_free(&compressed);
                }
            }

            if (!out_jpeg.empty() || !out_flate.empty()) {
                size_t encoded_size = out_jpeg.empty() ? out_flate.size() : out_jpeg.size();
                if (encoded_size >= stream_len && !is_smask_obj) {
                    fo = sanitize_raw_stream_dict(obj_id, gen, raw);
                } else {
                    remove_dict_entry(dict_str, "/Filter");
                    remove_dict_entry(dict_str, "/DecodeParms");
                    remove_dict_entry(dict_str, "/ColorSpace");
                    remove_dict_entry(dict_str, "/Length");
                        remove_dict_entry(dict_str, "/Decode");
                        remove_dict_entry(dict_str, "/BitsPerComponent");
                        remove_dict_entry(dict_str, "/Width");
                        remove_dict_entry(dict_str, "/Height");
                        while (dict_has(dict_str, "/Metadata")) remove_dict_entry(dict_str, "/Metadata");
                        while (dict_has(dict_str, "/PieceInfo")) remove_dict_entry(dict_str, "/PieceInfo");
                        while (dict_has(dict_str, "/Thumb")) remove_dict_entry(dict_str, "/Thumb");

                        std::string new_dict = dict_str;
                        size_t dd = new_dict.rfind(">>");
                        if (dd == std::string::npos) dd = new_dict.size();
                        
                        std::string filter = out_jpeg.empty() ? "/FlateDecode" : "/DCTDecode";
                        std::string bpc_str = "8";
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
                    fo = sanitize_raw_stream_dict(obj_id, gen, raw);
                    if (!skip_image) g_image_fallback++;
                }
            }
        }

    // Any image that cannot be safely decoded or made smaller is preserved.
    g_image_fallback++;
    fo = sanitize_raw_stream_dict(obj_id, gen, raw);
    return fo;
}

int rewrite_pdf_impl(pt_context *ctx, pt_state *st, const char *out_path) {
    if (!st->xref || st->xref->xref_store.empty()) return -1;

    s_cs_cache.clear();
    s_pal_cache.clear();
    s_devicen_cyk_cache.clear();
    s_dropped_ids.clear();
    s_smask_targets.clear();
    s_known_smasks.clear();

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

    int total_pages = 0;
    if (st->page && st->page->page_num > 0) {
        total_pages = (int)st->page->page_num;
    }
    if (total_pages <= 0 && st->xref && st->xref->root_obj > 0) {
        long root_offset = st->xref->look(ctx, st, st->xref->root_obj);
        if (root_offset > 0) {
            std::vector<uint8_t> r_raw = read_obj_raw(ctx, root_offset, root_offset + 4096);
            std::string rs(r_raw.begin(), r_raw.end());
            std::regex p_ref(R"(/Pages\s+(\d+)\s+(\d+)\s+R)");
            std::smatch pm;
            if (std::regex_search(rs, pm, p_ref)) {
                int pid = safe_stoi(pm[1].str());
                long poff = st->xref->look(ctx, st, pid);
                if (poff > 0) {
                    std::vector<uint8_t> praw = read_obj_raw(ctx, poff, poff + 4096);
                    std::string ps(praw.begin(), praw.end());
                    std::regex creg(R"(/Count\s+(\d+))");
                    std::smatch cm;
                    if (std::regex_search(ps, cm, creg)) {
                        total_pages = safe_stoi(cm[1].str());
                    }
                }
            }
        }
    }

    memset(s_used_codepoints, 0, sizeof(s_used_codepoints));
    g_font_codepoints.clear();
    for (size_t i = 0; i < slots.size(); ++i) {
        if (st->writer && st->writer->progress_cb && (i % 20 == 0 || i == 0 || i + 1 == slots.size())) {
            double prep_pct = (double)(i + 1) / slots.size() * 15.0;
            int display_total = (total_pages > 0) ? total_pages : (int)slots.size();
            int display_page = (total_pages > 0) ? std::max(1, (int)((double)(i + 1) / slots.size() * total_pages)) : (int)(i + 1);
            st->writer->progress_cb(display_page, display_total, prep_pct, st->writer->progress_user_data);
        }
        long end_hint = (i + 1 < slots.size()) ? slots[i + 1].offset : file_size;
        auto hdr_raw = read_obj_raw(ctx, slots[i].offset, std::min(end_hint, slots[i].offset + 1024));
        std::string hdr_s(hdr_raw.begin(), hdr_raw.end());

        if (hdr_s.find("/Type /Metadata") != std::string::npos ||
            hdr_s.find("/Type/Metadata") != std::string::npos ||
            hdr_s.find("/Subtype/XML") != std::string::npos) {
            s_dropped_ids.insert(slots[i].id);
        }
        collect_ref_ids_for_key(hdr_s, "/Metadata", s_dropped_ids);
        collect_ref_ids_for_key(hdr_s, "/Thumb", s_dropped_ids);
        collect_ref_ids_for_key(hdr_s, "/PieceInfo", s_dropped_ids);

        if (hdr_s.find("/Subtype /Image") != std::string::npos ||
            hdr_s.find("/Subtype/Image") != std::string::npos) {
            if (hdr_s.find("/SMask") != std::string::npos) {
                size_t sm_pos = hdr_s.find("/SMask");
                size_t p = sm_pos + 6;
                while (p < hdr_s.size() && isspace((unsigned char)hdr_s[p])) p++;
                size_t num_start = p;
                while (p < hdr_s.size() && isdigit((unsigned char)hdr_s[p])) p++;
                if (p > num_start) {
                    int smask_id = safe_stoi(hdr_s.substr(num_start, p - num_start));
                    while (p < hdr_s.size() && isspace((unsigned char)hdr_s[p])) p++;
                    while (p < hdr_s.size() && isdigit((unsigned char)hdr_s[p])) p++;
                    while (p < hdr_s.size() && isspace((unsigned char)hdr_s[p])) p++;
                    if (p < hdr_s.size() && hdr_s[p] == 'R' && smask_id > 0) {
                        long w = dict_int(hdr_s, "/Width", -1);
                        long h = dict_int(hdr_s, "/Height", -1);
                        if (w > 0 && h > 0) {
                            int target_dpi = (st->writer && st->writer->target_dpi > 0) ? st->writer->target_dpi : 150;
                            if (target_dpi < 50) target_dpi = 50;
                            int target_max_dim = (int)(target_dpi * 11.7);
                            if (target_max_dim < 600) target_max_dim = 600;
                            int max_dim = std::max(w, h);
                            double scale = (max_dim > target_max_dim) ? (double)target_max_dim / max_dim : 1.0;
                            long new_w = (long)(w * scale);
                            long new_h = (long)(h * scale);
                            if (new_w < 16) new_w = std::min((long)w, 16L);
                            if (new_h < 16) new_h = std::min((long)h, 16L);
                            s_smask_targets[smask_id] = { new_w, new_h };
                        }
                        s_known_smasks.insert(smask_id);
                    }
                }
            }
            continue;
        }
        if (hdr_s.find("/Type /XRef") != std::string::npos ||
            hdr_s.find("/Type/XRef") != std::string::npos) {
            continue;
        }
        auto raw = read_obj_raw(ctx, slots[i].offset, end_hint);
        if (!raw.empty()) {
            std::string s_full(raw.begin(), raw.end());
            collect_ref_ids_for_key(s_full, "/Metadata", s_dropped_ids);
            collect_ref_ids_for_key(s_full, "/Thumb", s_dropped_ids);
            collect_ref_ids_for_key(s_full, "/PieceInfo", s_dropped_ids);
            collect_tounicode_codepoints(raw);
            collect_used_codes_from_content(raw);
        }
    }

    int n = (int)slots.size();
    printf("writer: found %d direct objects, %d compressed entries, %zu metadata/thumb objects queued for removal\n",
           n, compressed_entries, s_dropped_ids.size());

    int current_page = 0;
    int last_reported_page = -1;

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

        int obj_id = slots[i].id;
        int gen_n  = slots[i].gen;
        if (s_dropped_ids.count(obj_id)) {
            continue;
        }

        std::string s(raw.begin(), raw.end());
        if (s.find("/Type /Page") != std::string::npos || s.find("/Type/Page") != std::string::npos) {
            current_page++;
        }
        if (s.find("/Type /XRef") != std::string::npos || s.find("/Type/XRef") != std::string::npos ||
            dict_has(s, "/Type /Metadata") || dict_has(s, "/Type/Metadata")) {
            s_dropped_ids.insert(obj_id);
            continue;
        }
        bool has_stream = s.find("stream") != std::string::npos;
        // Parse actual N G obj header from raw bytes - xref obj_id wrong for linearized PDFs
        {
            size_t p = 0;
            while (p < s.size() && isspace((unsigned char)s[p])) p++;
            size_t ns = p;
            while (p < s.size() && isdigit((unsigned char)s[p])) p++;
            if (p > ns) {
                int fid = safe_stoi(s.substr(ns, p-ns));
                while (p < s.size() && isspace((unsigned char)s[p])) p++;
                size_t gs = p;
                while (p < s.size() && isdigit((unsigned char)s[p])) p++;
                if (p > gs) {
                    int fg = safe_stoi(s.substr(gs, p-gs));
                    while (p < s.size() && isspace((unsigned char)s[p])) p++;
                    if (p+3 <= s.size() && s.substr(p,3) == "obj") { obj_id = fid; gen_n = fg; }
                }
            }
        }

        if (s_dropped_ids.count(obj_id)) {
            continue;
        }

        if (has_stream) {
            std::string dict_str;

            const uint8_t *sd; size_t sl;
            bool parsed = parse_stream_bounds(raw, dict_str, sd, sl);

            if (parsed && (dict_has(dict_str, "/Type /Metadata") || dict_has(dict_str, "/Type/Metadata") || s_dropped_ids.count(obj_id))) {
                s_dropped_ids.insert(obj_id);
                continue;
            }

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
                    if (s_dropped_ids.count(eid) || dict_has(ebody, "/Type /Metadata") || dict_has(ebody, "/Type/Metadata")) {
                        s_dropped_ids.insert(eid);
                        continue;
                    }
                    while (dict_has(ebody, "/Metadata")) remove_dict_entry(ebody, "/Metadata");
                    while (dict_has(ebody, "/PieceInfo")) remove_dict_entry(ebody, "/PieceInfo");
                    while (dict_has(ebody, "/Thumb")) remove_dict_entry(ebody, "/Thumb");
                    if (ebody.find("/Type /Page") != std::string::npos || ebody.find("/Type/Page") != std::string::npos) {
                        current_page++;
                    }
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
                const char *dptr = (const char*)fo.data.data();
                size_t dlen = fo.data.size();
                std::string_view sv(dptr, dlen);
                size_t epos = sv.rfind("endobj");
                if (epos != std::string_view::npos) {
                    size_t end_idx = epos + 6;
                    while (end_idx < dlen && (dptr[end_idx] == ' ' || dptr[end_idx] == '\t' || dptr[end_idx] == '\r' || dptr[end_idx] == '\n')) {
                        end_idx++;
                    }
                    fo.data.resize(end_idx);
                    if (fo.data.empty() || fo.data.back() != '\n') {
                        fo.data.push_back('\n');
                    }
                }
                output_objs[obj_id] = std::move(fo);
            }
        } else {
            if (s_dropped_ids.count(obj_id) || dict_has(s, "/Type /Metadata") || dict_has(s, "/Type/Metadata")) {
                continue;
            }
            while (dict_has(s, "/Metadata")) remove_dict_entry(s, "/Metadata");
            while (dict_has(s, "/PieceInfo")) remove_dict_entry(s, "/PieceInfo");
            while (dict_has(s, "/Thumb")) remove_dict_entry(s, "/Thumb");
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

        if (st->writer && st->writer->progress_cb) {
            int display_total = (total_pages > 0) ? total_pages : n;
            int display_page;
            if (total_pages > 0) {
                if (current_page > 0 && current_page <= total_pages) {
                    display_page = current_page;
                } else {
                    display_page = std::max(1, std::min((int)((double)(i + 1) / n * total_pages) + 1, total_pages));
                }
            } else {
                display_page = i + 1;
            }

            double pct = 15.0 + std::min(84.0, (double)(i + 1) / n * 84.0);
            bool is_image = has_stream && (s.find("/Subtype /Image") != std::string::npos || s.find("/Subtype/Image") != std::string::npos);
            if (display_page != last_reported_page || is_image || i == 0 || i == n - 1 || (i % 15 == 0)) {
                last_reported_page = display_page;
                st->writer->progress_cb(display_page, display_total, pct, st->writer->progress_user_data);
            }
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

    // -------------------------------------------------------------
    // Item 5: Deduplication of identical stream objects (Form XObjects, Images)
    // -------------------------------------------------------------
    std::unordered_map<int, int> dedup_map; // dup_id -> master_id
    {
        struct StreamFingerprint {
            size_t stream_len;
            uint32_t crc;
            std::string dict_sig;
            int master_id;
        };
        std::vector<StreamFingerprint> fingerprints;

        for (auto &kv : output_objs) {
            FlatObj &fo = kv.second;
            if (fo.data.empty() || fo.id <= 0) continue;
            std::string s_hdr(fo.data.begin(), fo.data.begin() + std::min((size_t)1024, fo.data.size()));
            if (s_hdr.find("stream") == std::string::npos) continue;

            const uint8_t *sd = nullptr; size_t sl = 0;
            if (!extract_stream(fo.data, &sd, &sl) || sl < 32) continue;

            uint32_t crc = (uint32_t)mz_crc32(0, sd, sl);

            std::string dict_str = extract_dict(fo.data);
            while (dict_has(dict_str, "/Length")) remove_dict_entry(dict_str, "/Length");
            while (dict_has(dict_str, "/Metadata")) remove_dict_entry(dict_str, "/Metadata");

            int match_master = -1;
            for (auto &fp : fingerprints) {
                if (fp.stream_len == sl && fp.crc == crc && fp.dict_sig == dict_str) {
                    FlatObj &master_fo = output_objs[fp.master_id];
                    const uint8_t *msd = nullptr; size_t msl = 0;
                    if (extract_stream(master_fo.data, &msd, &msl) && msl == sl && memcmp(msd, sd, sl) == 0) {
                        match_master = fp.master_id;
                        break;
                    }
                }
            }

            if (match_master > 0) {
                dedup_map[fo.id] = match_master;
            } else {
                fingerprints.push_back({ sl, crc, dict_str, fo.id });
            }
        }

        if (!dedup_map.empty()) {
            printf("writer: deduplication found %zu duplicate stream objects\n", dedup_map.size());
            for (auto &kv : output_objs) {
                FlatObj &fo = kv.second;
                if (fo.data.empty()) continue;

                std::string s_data(fo.data.begin(), fo.data.end());
                size_t stm_pos = s_data.find("stream");
                size_t dict_end = (stm_pos != std::string::npos) ? stm_pos : fo.data.size();

                std::string dict_part(fo.data.begin(), fo.data.begin() + dict_end);
                bool modified = false;

                for (auto &dp : dedup_map) {
                    int dup_id = dp.first;
                    int master_id = dp.second;
                    std::string target = std::to_string(dup_id) + " 0 R";
                    std::string repl   = std::to_string(master_id) + " 0 R";

                    size_t p = 0;
                    while ((p = dict_part.find(target, p)) != std::string::npos) {
                        if (p > 0 && isdigit((unsigned char)dict_part[p - 1])) {
                            p += target.size();
                            continue;
                        }
                        dict_part.replace(p, target.size(), repl);
                        p += repl.size();
                        modified = true;
                    }
                }

                if (modified) {
                    std::vector<uint8_t> new_data(dict_part.begin(), dict_part.end());
                    if (stm_pos != std::string::npos) {
                        new_data.insert(new_data.end(), fo.data.begin() + stm_pos, fo.data.end());
                    }
                    fo.data = std::move(new_data);
                }
            }

            for (auto &dp : dedup_map) {
                output_objs.erase(dp.first);
            }
        }
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

    bool use_stream = (!st->writer || !st->writer->traditional_xref);

    // -------------------------------------------------------------
    // Item 2: Pack non-stream objects into /Type /ObjStm containers
    // -------------------------------------------------------------
    struct PackedObjRef {
        int objstm_id;
        int index;
    };
    std::unordered_map<int, PackedObjRef> packed_obj_map;
    std::vector<FlatObj> objstm_containers_list;

    if (use_stream) {
        std::vector<std::pair<int, std::string>> eligible_non_streams;
        for (auto &kv : output_objs) {
            int oid = kv.first;
            FlatObj &fo = kv.second;
            if (fo.data.empty() || oid <= 0) continue;
            if (oid == root_obj || fo.gen != 0) continue;

            std::string s_obj(fo.data.begin(), fo.data.end());
            if (s_obj.find("stream") != std::string::npos) continue;
            if (s_obj.find("/Linearized") != std::string::npos ||
                s_obj.find("/Encrypt") != std::string::npos) continue;

            size_t p_obj = s_obj.find("obj");
            if (p_obj == std::string::npos) continue;
            p_obj += 3;
            while (p_obj < s_obj.size() && isspace((unsigned char)s_obj[p_obj])) p_obj++;

            size_t p_end = s_obj.rfind("endobj");
            if (p_end == std::string::npos || p_end <= p_obj) continue;
            while (p_end > p_obj && isspace((unsigned char)s_obj[p_end - 1])) p_end--;

            std::string body = s_obj.substr(p_obj, p_end - p_obj);
            if (!body.empty()) {
                eligible_non_streams.push_back({ oid, body });
            }
        }

        if (!eligible_non_streams.empty()) {
            const size_t OBJS_PER_CONTAINER = 100;
            size_t total_packed = 0;

            for (size_t chunk_start = 0; chunk_start < eligible_non_streams.size(); chunk_start += OBJS_PER_CONTAINER) {
                size_t chunk_end = std::min(chunk_start + OBJS_PER_CONTAINER, eligible_non_streams.size());
                int cur_objstm_id = ++max_id;

                std::string header_str;
                std::vector<uint8_t> body_bytes;

                for (size_t idx = 0; idx < (chunk_end - chunk_start); idx++) {
                    auto &item = eligible_non_streams[chunk_start + idx];
                    size_t cur_offset = body_bytes.size();
                    header_str += std::to_string(item.first) + " " + std::to_string(cur_offset) + " ";
                    body_bytes.insert(body_bytes.end(), item.second.begin(), item.second.end());
                    body_bytes.push_back('\n');

                    packed_obj_map[item.first] = { cur_objstm_id, (int)idx };
                    total_packed++;
                }

                size_t first_offset = header_str.size();
                std::vector<uint8_t> payload;
                payload.reserve(header_str.size() + body_bytes.size());
                payload.insert(payload.end(), header_str.begin(), header_str.end());
                payload.insert(payload.end(), body_bytes.begin(), body_bytes.end());

                std::vector<uint8_t> comp_payload = flate_compress(payload.data(), payload.size(), 6);
                if (comp_payload.empty()) comp_payload = payload;

                std::string stm_hdr = std::to_string(cur_objstm_id) + " 0 obj\n"
                                    + "<</Type/ObjStm/N " + std::to_string(chunk_end - chunk_start)
                                    + "/First " + std::to_string(first_offset)
                                    + "/Filter/FlateDecode/Length " + std::to_string(comp_payload.size()) + ">>stream\n";

                FlatObj c_fo;
                c_fo.id = cur_objstm_id;
                c_fo.gen = 0;
                c_fo.data.assign(stm_hdr.begin(), stm_hdr.end());
                c_fo.data.insert(c_fo.data.end(), comp_payload.begin(), comp_payload.end());
                std::string stm_tail = "\nendstream\nendobj\n";
                c_fo.data.insert(c_fo.data.end(), stm_tail.begin(), stm_tail.end());

                objstm_containers_list.push_back(std::move(c_fo));
            }

            printf("writer: packed %zu non-stream objects into %zu /ObjStm containers\n",
                   total_packed, objstm_containers_list.size());
        }
    }

    if (use_stream) {
        int xref_obj_id = max_id + 1;
        int total_size = xref_obj_id + 1;

        std::vector<uint64_t> xref_offsets(total_size, 0);
        std::vector<int>      xref_gens(total_size, 0);
        std::vector<int>      xref_types(total_size, 0);
        xref_gens[0] = 65535;

        // 1. Write direct objects (excluding objects that were packed into ObjStm)
        for (auto &kv : output_objs) {
            FlatObj &fo = kv.second;
            if (fo.data.empty() || fo.id <= 0 || fo.id >= total_size) continue;
            if (packed_obj_map.count(fo.id)) continue; // Packed in ObjStm!

            xref_offsets[fo.id] = (uint64_t)ftell(out);
            xref_gens[fo.id]    = fo.gen;
            xref_types[fo.id]   = 1; // Type 1: uncompressed direct object
            fwrite(fo.data.data(), 1, fo.data.size(), out);
        }

        // 2. Write ObjStm containers
        for (auto &c_fo : objstm_containers_list) {
            if (c_fo.id <= 0 || c_fo.id >= total_size) continue;
            xref_offsets[c_fo.id] = (uint64_t)ftell(out);
            xref_gens[c_fo.id]    = 0;
            xref_types[c_fo.id]   = 1;
            fwrite(c_fo.data.data(), 1, c_fo.data.size(), out);
        }

        // 3. Register Type 2 entries for packed objects
        for (auto &po : packed_obj_map) {
            int oid = po.first;
            if (oid > 0 && oid < total_size) {
                xref_offsets[oid] = (uint64_t)po.second.objstm_id; // Field 2: container ID
                xref_gens[oid]    = po.second.index;               // Field 3: index in container
                xref_types[oid]   = 2;                             // Type 2: object in ObjStm
            }
        }

        uint64_t xref_stream_offset = (uint64_t)ftell(out);
        xref_offsets[xref_obj_id] = xref_stream_offset;
        xref_gens[xref_obj_id]    = 0;
        xref_types[xref_obj_id]   = 1;

        int w2 = (xref_stream_offset > 0xFFFFFFFFULL) ? 8 : 4;
        int row_bytes = 1 + w2 + 2;

        std::vector<uint8_t> xref_raw;
        xref_raw.reserve((size_t)total_size * row_bytes);

        for (int i = 0; i < total_size; i++) {
            if (i == 0) {
                xref_raw.push_back(0);
                for (int b = 0; b < w2; b++) xref_raw.push_back(0);
                xref_raw.push_back(0xFF);
                xref_raw.push_back(0xFF);
            } else if (xref_types[i] == 2) {
                xref_raw.push_back(2); // Type 2: in ObjStm
                uint64_t stm_id = xref_offsets[i];
                for (int b = w2 - 1; b >= 0; b--) {
                    xref_raw.push_back((uint8_t)((stm_id >> (b * 8)) & 0xFF));
                }
                int idx = xref_gens[i];
                xref_raw.push_back((uint8_t)((idx >> 8) & 0xFF));
                xref_raw.push_back((uint8_t)(idx & 0xFF));
            } else if (xref_types[i] == 1) {
                xref_raw.push_back(1); // Type 1: direct
                uint64_t off = xref_offsets[i];
                for (int b = w2 - 1; b >= 0; b--) {
                    xref_raw.push_back((uint8_t)((off >> (b * 8)) & 0xFF));
                }
                int g = xref_gens[i];
                xref_raw.push_back((uint8_t)((g >> 8) & 0xFF));
                xref_raw.push_back((uint8_t)(g & 0xFF));
            } else {
                xref_raw.push_back(0); // Type 0: free
                for (int b = 0; b < w2; b++) xref_raw.push_back(0);
                xref_raw.push_back(0);
                xref_raw.push_back(0);
            }
        }

        std::vector<uint8_t> xref_comp = flate_compress(xref_raw.data(), xref_raw.size());

        std::string stream_hdr = std::to_string(xref_obj_id) + " 0 obj\n"
                               + "<</Type/XRef/Size " + std::to_string(total_size)
                               + "/W[1 " + std::to_string(w2) + " 2]";
        if (root_obj > 0) {
            stream_hdr += "/Root " + std::to_string(root_obj) + " " + std::to_string(root_gen) + " R";
        }

        if (!xref_comp.empty()) {
            stream_hdr += "/Filter/FlateDecode/Length " + std::to_string(xref_comp.size()) + ">>stream\n";
        } else {
            stream_hdr += "/Length " + std::to_string(xref_raw.size()) + ">>stream\n";
        }

        fwrite(stream_hdr.data(), 1, stream_hdr.size(), out);
        if (!xref_comp.empty()) {
            fwrite(xref_comp.data(), 1, xref_comp.size(), out);
        } else {
            fwrite(xref_raw.data(), 1, xref_raw.size(), out);
        }
        const char *stream_tail = "\nendstream\nendobj\n";
        fwrite(stream_tail, 1, strlen(stream_tail), out);

        fprintf(out, "startxref\n%llu\n%%%%EOF\n", (unsigned long long)xref_stream_offset);
    } else {
        int total_size = max_id + 1;
        std::vector<long> xref_offsets(total_size, 0);
        std::vector<int>  xref_gens(total_size, 0);
        std::vector<bool> xref_used(total_size, false);
        xref_gens[0] = 65535;

        for (auto &kv : output_objs) {
            FlatObj &fo = kv.second;
            if (fo.data.empty() || fo.id <= 0 || fo.id >= total_size) continue;
            xref_offsets[fo.id] = (long)ftell(out);
            xref_gens[fo.id]    = fo.gen;
            xref_used[fo.id]    = true;
            fwrite(fo.data.data(), 1, fo.data.size(), out);
        }

        long xref_start = (long)ftell(out);
        fprintf(out, "xref\n");
        fprintf(out, "0 %d\n", total_size);
        fprintf(out, "0000000000 65535 f \n");
        for (int i = 1; i < total_size; i++) {
            if (xref_used[i])
                fprintf(out, "%010ld %05d n \n", xref_offsets[i], xref_gens[i]);
            else
                fprintf(out, "0000000000 00000 f \n");
        }

        if (root_obj > 0)
            fprintf(out, "trailer\n<</Size %d/Root %d %d R>>\nstartxref\n%ld\n%%%%EOF\n",
                    total_size, root_obj, root_gen, xref_start);
    }
    fclose(out);

    if (st->writer && st->writer->progress_cb) {
        int display_total = (total_pages > 0) ? total_pages : n;
        st->writer->progress_cb(display_total, display_total, 100.0, st->writer->progress_user_data);
    }

    printf("writer: done -- output: %s\n", out_path);
    return 0;
}

void main_writer(pt_state *st) {
    if (st && st->writer) {
        st->writer->rewrite = rewrite_pdf_impl;
        st->writer->traditional_xref = false;
    }
}

static void collect_used_codes_from_content(const std::vector<uint8_t> &raw) {
    std::string dict_str = extract_dict(raw);
    if (dict_str.find("/Subtype /Image") != std::string::npos ||
        dict_str.find("/Subtype/Image") != std::string::npos ||
        dict_str.find("/Type /ObjStm") != std::string::npos ||
        dict_str.find("/Type/ObjStm") != std::string::npos ||
        dict_str.find("/Type /XRef") != std::string::npos ||
        dict_str.find("/FontFile") != std::string::npos) {
        return;
    }
    const uint8_t *stream_data = nullptr;
    size_t stream_len = 0;
    if (!extract_stream(raw, &stream_data, &stream_len)) return;
    if (stream_len == 0 || stream_len > 2000000) return;
    std::vector<uint8_t> decoded = dict_str.find("/FlateDecode") != std::string::npos
        ? flate_decompress(stream_data, stream_len)
        : std::vector<uint8_t>(stream_data, stream_data + stream_len);
    if (decoded.empty()) return;

    const char *p = (const char*)decoded.data();
    size_t len = decoded.size();
    size_t i = 0;
    while (i < len) {
        if (p[i] == '(') {
            i++;
            int depth = 1;
            while (i < len && depth > 0) {
                if (p[i] == '\\' && i + 1 < len) {
                    i += 2;
                } else if (p[i] == '(') {
                    depth++;
                    add_font_codepoint((uint8_t)p[i]);
                    i++;
                } else if (p[i] == ')') {
                    depth--;
                    if (depth > 0) add_font_codepoint((uint8_t)p[i]);
                    i++;
                } else {
                    add_font_codepoint((uint8_t)p[i]);
                    i++;
                }
            }
        } else if (p[i] == '<' && i + 1 < len && p[i+1] != '<') {
            i++;
            std::string hex;
            while (i < len && p[i] != '>') {
                if (!isspace((unsigned char)p[i])) hex.push_back(p[i]);
                i++;
            }
            if (i < len) i++;
            if (hex.size() % 4 == 0 && hex.size() >= 4) {
                for (size_t k = 0; k < hex.size(); k += 4) {
                    uint32_t cid = 0;
                    if (parse_hex_u32(hex.substr(k, 4), cid)) {
                        add_font_codepoint(cid);
                    }
                }
            } else if (hex.size() % 2 == 0) {
                for (size_t k = 0; k < hex.size(); k += 2) {
                    uint32_t c = 0;
                    if (parse_hex_u32(hex.substr(k, 2), c)) {
                        add_font_codepoint(c);
                    }
                }
            }
        } else {
            i++;
        }
    }
}

static void collect_tounicode_codepoints(const std::vector<uint8_t> &raw) {
    std::string dict_str = extract_dict(raw);
    if (dict_str.find("/ToUnicode") == std::string::npos &&
        dict_str.find("/CIDInit") == std::string::npos) return;
    const uint8_t *stream_data = nullptr;
    size_t stream_len = 0;
    if (!extract_stream(raw, &stream_data, &stream_len)) return;
    std::vector<uint8_t> decoded = dict_str.find("/FlateDecode") != std::string::npos
        ? flate_decompress(stream_data, stream_len)
        : std::vector<uint8_t>(stream_data, stream_data + stream_len);
    if (decoded.empty()) return;
    std::string cmap((const char*)decoded.data(), decoded.size());

    size_t pos = 0;
    while ((pos = cmap.find("beginbfchar", pos)) != std::string::npos) {
        pos += 11;
        size_t end_pos = cmap.find("endbfchar", pos);
        if (end_pos == std::string::npos) break;
        std::string sec = cmap.substr(pos, end_pos - pos);
        std::regex r(R"(<([0-9A-Fa-f]+)>\s+<([0-9A-Fa-f]+)>)");
        for (std::sregex_iterator it(sec.begin(), sec.end(), r), end; it != end; ++it) {
            uint32_t src = 0, dst = 0;
            if (parse_hex_u32((*it)[1].str(), src) && parse_hex_u32((*it)[2].str(), dst)) {
                add_font_codepoint(src);
                add_font_codepoint(dst);
            }
        }
        pos = end_pos + 9;
    }

    pos = 0;
    while ((pos = cmap.find("beginbfrange", pos)) != std::string::npos) {
        pos += 12;
        size_t end_pos = cmap.find("endbfrange", pos);
        if (end_pos == std::string::npos) break;
        std::string sec = cmap.substr(pos, end_pos - pos);
        std::regex r(R"(<([0-9A-Fa-f]+)>\s+<([0-9A-Fa-f]+)>\s+<([0-9A-Fa-f]+)>)");
        for (std::sregex_iterator it(sec.begin(), sec.end(), r), end; it != end; ++it) {
            uint32_t start = 0, end_c = 0, dst = 0;
            if (parse_hex_u32((*it)[1].str(), start) &&
                parse_hex_u32((*it)[2].str(), end_c) &&
                parse_hex_u32((*it)[3].str(), dst)) {
                if (end_c >= start && (end_c - start) < 65536) {
                    for (uint32_t c = start; c <= end_c; ++c) {
                        add_font_codepoint(c);
                        add_font_codepoint(dst + (c - start));
                    }
                }
            }
        }
        pos = end_pos + 10;
    }
}

static std::vector<uint8_t> parse_palette(pt_context *ctx, pt_state *st, int cs_id) {
    std::vector<uint8_t> pal;
    if (cs_id <= 0) return pal;
    auto it = s_pal_cache.find(cs_id);
    if (it != s_pal_cache.end()) return it->second;

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
        s_pal_cache[cs_id] = pal;
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
            ref_id = safe_stoi(s.substr(p));
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
    s_pal_cache[cs_id] = pal;
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
    int object_id = safe_stoi(match[1].str());
    auto it = s_cs_cache.find(object_id);
    if (it != s_cs_cache.end()) return it->second;

    long offset = st->xref->look(ctx, st, object_id);
    if (offset <= 0) return {};
    std::vector<uint8_t> raw = read_obj_raw(ctx, offset, offset + 100000);
    std::string s(raw.begin(), raw.end());
    size_t end_pos = s.find("endobj");
    if (end_pos != std::string::npos) s = s.substr(0, end_pos);
    s_cs_cache[object_id] = s;
    return s;
}