/*
    SMALLAB28 SOFTWARE
    ALLRIGHTS RESERVED
*/

#include "pation/state.h"
#include "pation/context.h"
#include "pdf/xref.h"
#include "miniz/miniz.h"
#include "pation/document.h"
#include "pdf/stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <vector>
#include <string>
#include <set>
#include <algorithm>
#include <iostream>

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

// Helper: Extract a key integer value from a PDF dictionary string
static long parse_dict_int(const std::string &dict, const std::string &key, long def = 0) {
    size_t p = dict.find(key);
    if (p == std::string::npos) return def;
    p += key.size();
    while (p < dict.size() && isspace((unsigned char)dict[p])) p++;
    if (p >= dict.size()) return def;

    // Check if indirect reference "N G R"
    size_t q = p;
    while (q < dict.size() && isdigit((unsigned char)dict[q])) q++;
    size_t q2 = q;
    while (q2 < dict.size() && isspace((unsigned char)dict[q2])) q2++;
    size_t q3 = q2;
    while (q3 < dict.size() && isdigit((unsigned char)dict[q3])) q3++;
    size_t q4 = q3;
    while (q4 < dict.size() && isspace((unsigned char)dict[q4])) q4++;
    if (q4 < dict.size() && dict[q4] == 'R') return def;

    char *end = nullptr;
    long val = strtol(&dict[p], &end, 10);
    if (end && end != &dict[p]) return val;
    return def;
}

// Helper: Extract /Root object ID and generation from dictionary string
static void parse_root_from_dict(pt_state *st, const std::string &dict) {
    if (st->xref->root_obj > 0) return;
    size_t p = dict.find("/Root");
    if (p == std::string::npos) return;
    p += 5;
    while (p < dict.size() && isspace((unsigned char)dict[p])) p++;
    if (p >= dict.size()) return;

    char *end = nullptr;
    long obj = strtol(&dict[p], &end, 10);
    if (end && end != &dict[p]) {
        while (*end && isspace((unsigned char)*end)) end++;
        long gen = strtol(end, &end, 10);
        st->xref->root_obj = (int)obj;
        st->xref->root_gen_num = (int)gen;
    }
}

static bool is_valid_xref_buffer(const char *buf, size_t n, int &out_type, size_t &out_match_pos) {
    for (size_t i = 0; i + 4 <= n; i++) {
        if (strncmp(buf + i, "xref", 4) == 0) {
            bool pre_ok = (i == 0) || isspace((unsigned char)buf[i - 1]);
            bool post_ok = (i + 4 == n) || isspace((unsigned char)buf[i + 4]);
            if (pre_ok && post_ok) {
                out_type = PT_DOC_TABLE;
                out_match_pos = i;
                return true;
            }
        }
    }
    // Check for "N G obj" where N and G are numbers
    for (size_t i = 0; i + 5 <= n; i++) {
        if (isdigit((unsigned char)buf[i])) {
            char *end = nullptr;
            long num1 = strtol(buf + i, &end, 10);
            if (end && end != buf + i && num1 >= 0) {
                while (*end && isspace((unsigned char)*end)) end++;
                long num2 = strtol(end, &end, 10);
                (void)num2;
                while (*end && isspace((unsigned char)*end)) end++;
                if (strncmp(end, "obj", 3) == 0) {
                    char post = end[3];
                    if (post == 0 || isspace((unsigned char)post) || post == '<' || post == '/') {
                        out_type = PT_DOC_STREAM;
                        out_match_pos = i;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// Detect whether the xref at `offset` is a traditional XRef table or an XRef stream,
// with automatic window scanning tolerance for slightly misaligned offsets.
static int detect_xref_type_at(pt_context *ctx, long &offset) {
    if (offset <= 0) return PT_DOC_INVALID;
    if (ctx->file->seek(ctx, ctx->file->f, offset, PT_SEEK_SET) != 0) return PT_DOC_INVALID;

    char buf[128] = {0};
    size_t n = ctx->file->read(ctx, buf, 1, 127, ctx->file->f);
    if (n > 0) {
        size_t p = 0;
        while (p < n && isspace((unsigned char)buf[p])) p++;
        if (p < n) {
            if (strncmp(buf + p, "xref", 4) == 0) {
                char next = buf[p + 4];
                if (next == 0 || isspace((unsigned char)next)) {
                    offset += (long)p;
                    return PT_DOC_TABLE;
                }
            }
            char *end = nullptr;
            long obj_num = strtol(buf + p, &end, 10);
            if (end && end != buf + p && obj_num >= 0) {
                while (*end && isspace((unsigned char)*end)) end++;
                strtol(end, &end, 10);
                while (*end && isspace((unsigned char)*end)) end++;
                if (strncmp(end, "obj", 3) == 0) {
                    offset += (long)p;
                    return PT_DOC_STREAM;
                }
            }
        }
    }

    // Offset misalignment recovery: scan a +/- 4096-byte window around offset
    ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_END);
    long file_size = ctx->file->tell(ctx, ctx->file->f);
    long window_start = std::max(0L, offset - 4096L);
    long window_end = std::min(file_size, offset + 4096L);
    size_t window_len = (size_t)(window_end - window_start);

    if (window_len > 0 && ctx->file->seek(ctx, ctx->file->f, window_start, PT_SEEK_SET) == 0) {
        std::vector<char> win_buf(window_len + 1, 0);
        size_t r = ctx->file->read(ctx, win_buf.data(), 1, window_len, ctx->file->f);
        if (r > 0) {
            int recovered_type = PT_DOC_INVALID;
            size_t match_pos = 0;
            if (is_valid_xref_buffer(win_buf.data(), r, recovered_type, match_pos)) {
                offset = window_start + (long)match_pos;
                return recovered_type;
            }
        }
    }

    return PT_DOC_INVALID;
}

// Public API for checking type
int old_or_new_xref(pt_context *ctx, pt_state *st) {
    if (st->xref->start_xref <= 0) {
        st->xref->find(ctx, st);
    }
    if (st->xref->start_xref <= 0) return ctx->doc = PT_DOC_INVALID;
    int type = detect_xref_type_at(ctx, st->xref->start_xref);
    return ctx->doc = (pt_doc)type;
}

// Locate startxref from the end of the file, expanding search window if needed
long find_xref_table(pt_context *ctx, pt_state *st) {
    if (!ctx || !ctx->file || !ctx->file->f) return 0;

    ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_END);
    long file_size = ctx->file->tell(ctx, ctx->file->f);
    if (file_size <= 0) return 0;

    // Scan backward progressively: 1KB, 4KB, 16KB, 64KB, up to 512KB
    long scan_sizes[] = { 1024, 4096, 16384, 65536, 524288 };
    long found_offset = 0;

    for (long scan_size : scan_sizes) {
        if (scan_size > file_size) scan_size = file_size;

        if (ctx->file->seek(ctx, ctx->file->f, -scan_size, PT_SEEK_END) != 0) continue;
        std::vector<char> buffer(scan_size + 1, 0);
        size_t bytes_read = ctx->file->read(ctx, buffer.data(), 1, scan_size, ctx->file->f);
        if (bytes_read == 0) continue;

        std::string s(buffer.data(), bytes_read);
        size_t p = s.rfind("startxref");
        if (p != std::string::npos) {
            size_t num_start = p + 9;
            while (num_start < s.size()) {
                if (isspace((unsigned char)s[num_start])) {
                    num_start++;
                } else if (s[num_start] == '%') {
                    while (num_start < s.size() && s[num_start] != '\r' && s[num_start] != '\n') {
                        num_start++;
                    }
                } else {
                    break;
                }
            }
            if (num_start < s.size() && isdigit((unsigned char)s[num_start])) {
                try {
                    found_offset = std::stol(s.substr(num_start));
                    if (found_offset > 0 && found_offset < file_size) {
                        break;
                    }
                } catch (...) {
                    found_offset = 0;
                }
            }
        }
        if (scan_size >= file_size) break;
    }

    st->xref->start_xref = found_offset;
    return found_offset;
}

// Tokenizer for parsing traditional XRef tables (handles arbitrary spacing and comments)
struct XrefTokenizer {
    pt_context *ctx;
    std::vector<char> buf;
    size_t pos;
    size_t len;
    bool at_eof;

    XrefTokenizer(pt_context *c) : ctx(c), buf(8192), pos(0), len(0), at_eof(false) {}

    void fill() {
        if (at_eof) return;
        len = ctx->file->read(ctx, buf.data(), 1, buf.size(), ctx->file->f);
        pos = 0;
        if (len == 0) at_eof = true;
    }

    bool next(std::string &tok) {
        tok.clear();
        while (true) {
            while (pos < len && isspace((unsigned char)buf[pos])) pos++;
            if (pos >= len) {
                if (at_eof) return !tok.empty();
                fill();
                if (at_eof) return !tok.empty();
                continue;
            }
            if (buf[pos] == '%') {
                while (pos < len && buf[pos] != '\r' && buf[pos] != '\n') pos++;
                continue;
            }
            break;
        }

        while (true) {
            while (pos < len && !isspace((unsigned char)buf[pos])) {
                tok.push_back(buf[pos++]);
            }
            if (pos >= len) {
                if (at_eof) break;
                fill();
                if (at_eof) break;
                continue;
            }
            break;
        }
        return !tok.empty();
    }
};

// Parse a traditional XRef table (all subsections) and its trailer dictionary
static bool parse_xref_table_at(pt_context *ctx, pt_state *st, long table_offset,
                                long &out_prev, long &out_xref_stm) {
    out_prev = 0;
    out_xref_stm = 0;

    if (ctx->file->seek(ctx, ctx->file->f, table_offset, PT_SEEK_SET) != 0) return false;

    XrefTokenizer tok(ctx);
    std::string t;
    if (!tok.next(t)) return false;

    // First token must be "xref"
    if (t != "xref") {
        if (t.find("xref") != std::string::npos) {
            // "xref" had no leading whitespace or was fused
        } else {
            return false;
        }
    }

    // Process subsections until "trailer"
    while (tok.next(t)) {
        if (t == "trailer") {
            // Read trailer dictionary
            long trailer_file_pos = ctx->file->tell(ctx, ctx->file->f);
            if (tok.pos < tok.len) {
                trailer_file_pos -= (long)(tok.len - tok.pos);
            }
            ctx->file->seek(ctx, ctx->file->f, trailer_file_pos, PT_SEEK_SET);

            std::vector<char> dict_buf(8192, 0);
            size_t n = ctx->file->read(ctx, dict_buf.data(), 1, dict_buf.size() - 1, ctx->file->f);
            std::string trailer_str(dict_buf.data(), n);

            size_t dict_start = trailer_str.find("<<");
            if (dict_start != std::string::npos) {
                int depth = 0;
                size_t dict_end = dict_start;
                while (dict_end + 1 < trailer_str.size()) {
                    if (trailer_str[dict_end] == '<' && trailer_str[dict_end + 1] == '<') {
                        depth++;
                        dict_end += 2;
                    } else if (trailer_str[dict_end] == '>' && trailer_str[dict_end + 1] == '>') {
                        depth--;
                        dict_end += 2;
                        if (depth == 0) break;
                    } else {
                        dict_end++;
                    }
                }
                std::string dict = trailer_str.substr(dict_start, dict_end - dict_start);
                parse_root_from_dict(st, dict);
                out_prev = parse_dict_int(dict, "/Prev", 0);
                out_xref_stm = parse_dict_int(dict, "/XRefStm", 0);
            }
            break;
        }

        // Must be subsection header: <start_obj> <count>
        char *end = nullptr;
        int start_obj = (int)strtol(t.c_str(), &end, 10);
        if (!end || end == t.c_str()) break;
        if (!tok.next(t)) break;
        int count = (int)strtol(t.c_str(), &end, 10);
        if (!end || end == t.c_str()) break;

        if (count < 0 || count > 10000000) break;

        int required_size = start_obj + count + 1;
        if (required_size > (int)st->xref->xref_store.size()) {
            st->xref->xref_store.resize(required_size);
        }

        for (int i = 0; i < count; i++) {
            std::string t_offset, t_gen, t_status;
            if (!tok.next(t_offset) || !tok.next(t_gen) || !tok.next(t_status)) break;

            long offset = strtol(t_offset.c_str(), &end, 10);
            if (!end || end == t_offset.c_str()) continue;
            int gen = (int)strtol(t_gen.c_str(), &end, 10);
            if (!end || end == t_gen.c_str()) continue;
            char status = t_status.empty() ? 'f' : t_status[0];

            int obj_id = start_obj + i;
            if (obj_id >= (int)st->xref->xref_store.size()) {
                st->xref->xref_store.resize(obj_id + 1);
            }

            // Keep newer revision (do not overwrite if already populated)
            if (st->xref->xref_store[obj_id].byte_offset == 0 && obj_id != 0) {
                st->xref->xref_store[obj_id].obj_id = obj_id;
                st->xref->xref_store[obj_id].byte_offset = offset;
                st->xref->xref_store[obj_id].gen_num = gen;
                st->xref->xref_store[obj_id].status_obj = status;
                st->xref->xref_store[obj_id].stream_type_f1 = (status == 'f') ? 0 : 1;
            }
        }
    }

    return true;
}

// Backward-compatible parse_xref_table wrapper
int parse_xref_table(pt_context *ctx, pt_state *st) {
    long prev = 0, xstm = 0;
    if (parse_xref_table_at(ctx, st, st->xref->start_xref, prev, xstm)) {
        return ctx->doc = PT_DOC_OK;
    }
    return ctx->doc = PT_DOC_ERR;
}

// Parse an XRef Stream object (PDF 1.5+)
static bool parse_xref_stream_at(pt_context *ctx, pt_state *st, long stream_offset, long &out_prev) {
    out_prev = 0;
    if (ctx->file->seek(ctx, ctx->file->f, stream_offset, PT_SEEK_SET) != 0) return false;

    // Read initial chunk to parse dictionary and locate "stream"
    std::vector<char> header_buf(65536, 0);
    size_t header_read = ctx->file->read(ctx, header_buf.data(), 1, header_buf.size() - 1, ctx->file->f);
    if (header_read == 0) return false;

    std::string header(header_buf.data(), header_read);
    size_t dict_start = header.find("<<");
    if (dict_start == std::string::npos) return false;

    int depth = 0;
    size_t dict_end = dict_start;
    while (dict_end + 1 < header.size()) {
        if (header[dict_end] == '<' && header[dict_end + 1] == '<') {
            depth++;
            dict_end += 2;
        } else if (header[dict_end] == '>' && header[dict_end + 1] == '>') {
            depth--;
            dict_end += 2;
            if (depth == 0) break;
        } else {
            dict_end++;
        }
    }
    if (depth != 0) return false;

    std::string dict = header.substr(dict_start, dict_end - dict_start);

    parse_root_from_dict(st, dict);
    out_prev = parse_dict_int(dict, "/Prev", 0);

    long length = parse_dict_int(dict, "/Length", 0);
    if (length <= 0) return false;

    // Parse /W array: [ w1 w2 w3 ]
    uint8 w1 = 0, w2 = 0, w3 = 0;
    size_t w_pos = dict.find("/W");
    if (w_pos != std::string::npos) {
        size_t arr_start = dict.find('[', w_pos);
        if (arr_start != std::string::npos && arr_start < dict.size()) {
            char *p = (char*)dict.data() + arr_start + 1;
            char *end = nullptr;
            w1 = (uint8)strtol(p, &end, 10);
            p = end;
            w2 = (uint8)strtol(p, &end, 10);
            p = end;
            w3 = (uint8)strtol(p, &end, 10);
        }
    }
    int row_size = (int)w1 + (int)w2 + (int)w3;
    if (row_size <= 0) return false;

    st->xref->w1 = w1;
    st->xref->w2 = w2;
    st->xref->w3 = w3;
    st->xref->row_size = (uint8)row_size;

    // Parse /Index chunks: [ start1 count1 start2 count2 ... ]
    std::vector<std::pair<int, int>> chunks;
    size_t idx_pos = dict.find("/Index");
    if (idx_pos != std::string::npos) {
        size_t arr_start = dict.find('[', idx_pos);
        if (arr_start != std::string::npos) {
            char *p = (char*)dict.data() + arr_start + 1;
            while (*p && *p != ']') {
                char *end = nullptr;
                int start_obj = strtol(p, &end, 10);
                if (p == end) break;
                p = end;
                while (*p && isspace((unsigned char)*p)) p++;
                int count = strtol(p, &end, 10);
                if (p == end) break;
                p = end;
                while (*p && isspace((unsigned char)*p)) p++;
                if (count > 0) chunks.push_back({start_obj, count});
            }
        }
    }
    if (chunks.empty()) {
        long size_val = parse_dict_int(dict, "/Size", 0);
        if (size_val > 0) chunks.push_back({0, (int)size_val});
    }

    // Locate "stream" keyword (must be after dict_end)
    size_t stream_kw = header.find("stream", dict_end);
    if (stream_kw == std::string::npos) return false;
    stream_kw += 6;
    if (stream_kw < header.size() && header[stream_kw] == '\r') stream_kw++;
    if (stream_kw < header.size() && header[stream_kw] == '\n') stream_kw++;

    long stream_data_start = stream_offset + (long)stream_kw;
    if (ctx->file->seek(ctx, ctx->file->f, stream_data_start, PT_SEEK_SET) != 0) return false;

    std::vector<unsigned char> raw_stream((size_t)length);
    size_t bytes_read = ctx->file->u_read(ctx, raw_stream.data(), 1, (size_t)length, ctx->file->f);
    if (bytes_read != (size_t)length) return false;

    // Decompress stream
    std::vector<unsigned char> stream_data;
    if (dict.find("/FlateDecode") != std::string::npos) {
        size_t out_len = 0;
        void *out = tinfl_decompress_mem_to_heap(raw_stream.data(), raw_stream.size(),
                                                  &out_len, TINFL_FLAG_PARSE_ZLIB_HEADER);
        if (!out) return false;
        stream_data.assign((unsigned char*)out, (unsigned char*)out + out_len);
        mz_free(out);
    } else {
        stream_data = std::move(raw_stream);
    }

    // Decode Predictor (PNG Predictor 10-15 & TIFF Predictor 2)
    long predictor = parse_dict_int(dict, "/Predictor", 1);
    long columns = parse_dict_int(dict, "/Columns", row_size);
    if (columns <= 0) columns = row_size;

    if (predictor >= 10 && columns > 0) {
        int row_bytes = (int)columns + 1;
        std::vector<unsigned char> decoded;
        decoded.reserve((stream_data.size() / row_bytes) * columns);

        std::vector<unsigned char> prev_row(columns, 0);
        for (size_t r = 0; r + row_bytes <= stream_data.size(); r += row_bytes) {
            int pred_type = stream_data[r];
            std::vector<unsigned char> cur_row(columns, 0);
            for (int c = 0; c < columns; c++) {
                unsigned char raw = stream_data[r + 1 + c];
                unsigned char left = (c > 0) ? cur_row[c - 1] : 0;
                unsigned char up = prev_row[c];
                unsigned char upleft = (c > 0) ? prev_row[c - 1] : 0;

                unsigned char val = 0;
                if (pred_type == 0) { // None
                    val = raw;
                } else if (pred_type == 1) { // Sub
                    val = raw + left;
                } else if (pred_type == 2) { // Up
                    val = raw + up;
                } else if (pred_type == 3) { // Average
                    val = raw + (unsigned char)(((int)left + (int)up) / 2);
                } else if (pred_type == 4) { // Paeth
                    int p = (int)left + (int)up - (int)upleft;
                    int pa = abs(p - (int)left);
                    int pb = abs(p - (int)up);
                    int pc = abs(p - (int)upleft);
                    unsigned char pr;
                    if (pa <= pb && pa <= pc) pr = left;
                    else if (pb <= pc) pr = up;
                    else pr = upleft;
                    val = raw + pr;
                } else {
                    val = raw + up;
                }
                cur_row[c] = val;
                decoded.push_back(val);
            }
            prev_row = cur_row;
        }
        stream_data = std::move(decoded);
    } else if (predictor == 2 && columns > 0) {
        // TIFF Predictor 2
        for (size_t r = 0; r + columns <= stream_data.size(); r += columns) {
            for (int c = 1; c < columns; c++) {
                stream_data[r + c] += stream_data[r + c - 1];
            }
        }
    }

    // Populate xref_store according to chunks
    size_t data_pos = 0;
    for (const auto &chunk : chunks) {
        int start_obj = chunk.first;
        int count = chunk.second;
        int new_size = start_obj + count + 1;
        if (new_size > (int)st->xref->xref_store.size()) {
            st->xref->xref_store.resize(new_size);
        }

        for (int cur_id = start_obj; cur_id < start_obj + count; cur_id++) {
            if (data_pos + row_size > stream_data.size()) break;

            long type = (w1 == 0) ? 1 : 0;
            for (int j = 0; j < w1; j++) {
                type = (type << 8) | stream_data[data_pos + j];
            }

            long field2 = 0;
            for (int j = 0; j < w2; j++) {
                field2 = (field2 << 8) | stream_data[data_pos + w1 + j];
            }

            long field3 = 0;
            for (int j = 0; j < w3; j++) {
                field3 = (field3 << 8) | stream_data[data_pos + w1 + w2 + j];
            }

            if (st->xref->xref_store[cur_id].byte_offset == 0 && cur_id != 0) {
                st->xref->xref_store[cur_id].obj_id = cur_id;
                st->xref->xref_store[cur_id].stream_type_f1 = (uint8)type;
                st->xref->xref_store[cur_id].byte_offset = field2;
                st->xref->xref_store[cur_id].gen_num = (int)field3;
                st->xref->xref_store[cur_id].status_obj = (type == 0) ? 'f' : 'n';
            }
            data_pos += row_size;
        }
    }

    return true;
}

// Backward-compatible parse_xref_stream wrapper
std::vector<unsigned char> parse_xref_stream(pt_context *ctx, pt_state *st) {
    long prev = 0;
    if (parse_xref_stream_at(ctx, st, st->xref->start_xref, prev)) {
        return std::vector<unsigned char>(1, 0);
    }
    return std::vector<unsigned char>();
}

// Lookup byte offset for a target object ID
long lookup_offset(pt_context *ctx, pt_state *st, int target_obj) {
    if (!st || !st->xref) return 0;
    if (target_obj < 0 || target_obj >= (int)st->xref->xref_store.size()) {
        return 0;
    }
    return st->xref->xref_store[target_obj].byte_offset;
}

// Build complete XRef dictionary by traversing all revisions (/Prev and /XRefStm)
int dictionary_xref(pt_context *ctx, pt_state *st) {
    if (st->xref->start_xref <= 0) {
        find_xref_table(ctx, st);
    }
    if (st->xref->start_xref <= 0) return ctx->doc = PT_DOC_INVALID;

    std::vector<long> queue;
    std::set<long> visited;

    queue.push_back(st->xref->start_xref);
    int initial_doc_type = detect_xref_type_at(ctx, st->xref->start_xref);

    while (!queue.empty()) {
        long current_xref = queue.front();
        queue.erase(queue.begin());

        if (current_xref <= 0 || visited.count(current_xref)) continue;
        visited.insert(current_xref);

        int type = detect_xref_type_at(ctx, current_xref);
        long prev_xref = 0;
        long xref_stm = 0;

        if (type == PT_DOC_TABLE) {
            parse_xref_table_at(ctx, st, current_xref, prev_xref, xref_stm);
            if (xref_stm > 0 && !visited.count(xref_stm)) {
                queue.push_back(xref_stm);
            }
            if (prev_xref > 0 && !visited.count(prev_xref)) {
                queue.push_back(prev_xref);
            }
        } else if (type == PT_DOC_STREAM) {
            parse_xref_stream_at(ctx, st, current_xref, prev_xref);
            if (prev_xref > 0 && !visited.count(prev_xref)) {
                queue.push_back(prev_xref);
            }
        }
    }

    st->xref->total_entries = (int)st->xref->xref_store.size();

    if (!st->xref->xref_store.empty() && (initial_doc_type == PT_DOC_TABLE || initial_doc_type == PT_DOC_STREAM)) {
        return ctx->doc = (pt_doc)initial_doc_type;
    }
    return ctx->doc = PT_DOC_INVALID;
}

// Parse trailer dictionary to extract /Root object
int parse_trailer(pt_context *ctx, pt_state *st) {
    if (st->xref->root_obj > 0) {
        return ctx->doc = (pt_doc)detect_xref_type_at(ctx, st->xref->start_xref);
    }

    int type = detect_xref_type_at(ctx, st->xref->start_xref);
    if (type == PT_DOC_TABLE) {
        ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_END);
        long sz = ctx->file->tell(ctx, ctx->file->f);
        long read_sz = std::min(sz, 4096L);
        if (ctx->file->seek(ctx, ctx->file->f, -read_sz, PT_SEEK_END) == 0) {
            std::vector<char> buf(read_sz + 1, 0);
            size_t n = ctx->file->read(ctx, buf.data(), 1, read_sz, ctx->file->f);
            if (n > 0) {
                std::string s(buf.data(), n);
                parse_root_from_dict(st, s);
            }
        }
        return ctx->doc = PT_DOC_TABLE;
    } else {
        if (ctx->file->seek(ctx, ctx->file->f, st->xref->start_xref, PT_SEEK_SET) == 0) {
            std::vector<char> buf(4096, 0);
            size_t n = ctx->file->read(ctx, buf.data(), 1, 4095, ctx->file->f);
            if (n > 0) {
                std::string s(buf.data(), n);
                parse_root_from_dict(st, s);
            }
        }
        return ctx->doc = PT_DOC_STREAM;
    }
}

void main_xref(pt_state *st) {
    st->xref->root_obj = 0;
    st->xref->root_gen_num = 0;
    st->xref->find = find_xref_table;
    st->xref->xref_table = parse_xref_table;
    st->xref->xref_stream = parse_xref_stream;
    st->xref->dictionary = dictionary_xref;
    st->xref->trailer = parse_trailer;
    st->xref->look = lookup_offset;
    st->xref->main = main_xref;
}
