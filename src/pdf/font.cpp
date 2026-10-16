#include "pdf/font.h"
#include <stdlib.h>
#include <string.h>
#include <cstring>
#include <stdint.h>
#include <stdio.h>
#include <vector>

static inline uint16_t be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline void put16(uint8_t *p, uint16_t v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

static inline void put32(uint8_t *p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

int pt_font_open(pt_font *font, const uint8_t *data, size_t size) {
    memset(font, 0, sizeof(*font));
    if (size < 12) return -1;
    font->data = data;
    font->size = size;

    uint32_t sfver = be32(data);
    if (sfver != 0x00010000 && sfver != PT_TAG('t','r','u','e') &&
        sfver != PT_TAG('O','T','T','O')) {
        return -1;
    }

    font->num_tables = be16(data + 4);
    font->tables = (pt_sfnt_table*)calloc(font->num_tables, sizeof(pt_sfnt_table));
    if (!font->tables) return -1;

    const uint8_t *entry = data + 12;
    for (int i = 0; i < font->num_tables; i++, entry += 16) {
        font->tables[i].tag      = be32(entry);
        font->tables[i].checksum = be32(entry + 4);
        font->tables[i].offset   = be32(entry + 8);
        font->tables[i].length   = be32(entry + 12);

        uint32_t tag = font->tables[i].tag;
        uint32_t off = font->tables[i].offset;
        uint32_t len = font->tables[i].length;

        if      (tag == PT_TAG_head) font->off_head = off;
        else if (tag == PT_TAG_hhea) font->off_hhea = off;
        else if (tag == PT_TAG_maxp) font->off_maxp = off;
        else if (tag == PT_TAG_cmap) font->off_cmap = off;
        else if (tag == PT_TAG_loca) font->off_loca = off;
        else if (tag == PT_TAG_glyf) font->off_glyf = off;
        else if (tag == PT_TAG_hmtx) font->off_hmtx = off;
        else if (tag == PT_TAG_OS2)  font->off_OS2  = off;
        else if (tag == PT_TAG_name) font->off_name = off;
        else if (tag == PT_TAG_post) font->off_post = off;
        else if (tag == PT_TAG_cvt)  { font->off_cvt  = off; font->len_cvt  = len; }
        else if (tag == PT_TAG_fpgm) { font->off_fpgm = off; font->len_fpgm = len; }
        else if (tag == PT_TAG_prep) { font->off_prep = off; font->len_prep = len; }
    }

    if (font->off_maxp && font->off_maxp + 6 <= size)
        font->num_glyphs = be16(data + font->off_maxp + 4);

    if (font->off_head && font->off_head + 52 <= size)
        font->index_to_loc_format = be16(data + font->off_head + 50);

    if (font->off_hhea && font->off_hhea + 36 <= size)
        font->num_h_metrics = be16(data + font->off_hhea + 34);

    return 0;
}

void pt_font_close(pt_font *font) {
    if (font->tables) {
        free(font->tables);
        font->tables = NULL;
    }
    memset(font, 0, sizeof(*font));
}

pt_glyph_set pt_glyph_set_create(uint16_t num_glyphs) {
    pt_glyph_set gs;
    gs.cap  = num_glyphs;
    gs.used = (bool*)calloc(num_glyphs, sizeof(bool));
    return gs;
}

void pt_glyph_set_free(pt_glyph_set *gs) {
    if (gs->used) {
        free(gs->used);
        gs->used = NULL;
    }
}

static uint16_t cmap_lookup_f4(const uint8_t *seg, uint16_t codepoint, int seg_count) {
    uint16_t segCountX2 = be16(seg + 6);
    int sc = segCountX2 / 2;
    const uint8_t *end_code     = seg + 14;
    const uint8_t *start_code   = end_code + sc * 2 + 2;
    const uint8_t *id_delta     = start_code + sc * 2;
    const uint8_t *id_range_off = id_delta + sc * 2;

    for (int i = 0; i < sc; i++) {
        uint16_t end   = be16(end_code + i * 2);
        uint16_t start = be16(start_code + i * 2);
        if (codepoint > end) continue;
        if (codepoint < start) return 0;
        uint16_t delta = be16(id_delta + i * 2);
        uint16_t roff  = be16(id_range_off + i * 2);
        if (roff == 0) {
            return (uint16_t)((codepoint + delta) & 0xFFFF);
        } else {
            const uint8_t *p = id_range_off + i * 2 + roff + (codepoint - start) * 2;
            uint16_t gid = be16(p);
            if (gid == 0) return 0;
            return (uint16_t)((gid + delta) & 0xFFFF);
        }
    }
    return 0;
}

void pt_cmap_mark(const pt_font *font, pt_glyph_set *gs,
                  const uint32_t *codepoints, size_t count) {
    if (gs->cap > 0) gs->used[0] = true;

    for (size_t k = 0; k < count; k++) {
        if (codepoints[k] < gs->cap) {
            gs->used[codepoints[k]] = true;
        }
    }

    if (!font->off_cmap) return;
    const uint8_t *cmap = font->data + font->off_cmap;
    uint16_t n_subtables = be16(cmap + 2);

    const uint8_t *chosen = NULL;
    for (int i = 0; i < n_subtables; i++) {
        const uint8_t *rec = cmap + 4 + i * 8;
        uint16_t plat = be16(rec);
        uint16_t enc  = be16(rec + 2);
        uint32_t off  = be32(rec + 4);
        const uint8_t *sub = cmap + off;
        uint16_t fmt = be16(sub);
        if (fmt == 4 && ((plat == 3 && enc == 1) || (plat == 0))) {
            chosen = sub;
            if (plat == 3 && enc == 1) break;
        }
    }
    if (!chosen) return;

    for (size_t k = 0; k < count; k++) {
        if (codepoints[k] > 0xFFFF) continue;
        uint16_t gid = cmap_lookup_f4(chosen, (uint16_t)codepoints[k],
                                      be16(chosen + 6) / 2);
        if (gid > 0 && gid < gs->cap)
            gs->used[gid] = true;
    }
}

static uint32_t loca_offset(const pt_font *font, uint16_t gid) {
    if (font->index_to_loc_format == 0)
        return (uint32_t)be16(font->data + font->off_loca + gid * 2) * 2;
    return be32(font->data + font->off_loca + gid * 4);
}

static uint32_t loca_length(const pt_font *font, uint16_t gid) {
    uint32_t a = loca_offset(font, gid);
    uint32_t b = loca_offset(font, gid + 1);
    return (b > a) ? (b - a) : 0;
}

#define ARG_1_AND_2_ARE_WORDS    (1 << 0)
#define MORE_COMPONENTS          (1 << 5)
#define WE_HAVE_A_SCALE          (1 << 3)
#define WE_HAVE_AN_X_AND_Y_SCALE (1 << 6)
#define WE_HAVE_A_TWO_BY_TWO     (1 << 7)

void pt_glyf_closure(const pt_font *font, pt_glyph_set *gs) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (uint16_t gid = 0; gid < gs->cap; gid++) {
            if (!gs->used[gid]) continue;
            uint32_t off = font->off_glyf + loca_offset(font, gid);
            uint32_t len = loca_length(font, gid);
            if (len < 10) continue;
            const uint8_t *g = font->data + off;
            int16_t n_contours = (int16_t)be16(g);
            if (n_contours >= 0) continue;

            const uint8_t *p = g + 10;
            uint16_t flags;
            do {
                flags = be16(p); p += 2;
                uint16_t comp_gid = be16(p); p += 2;
                if (flags & ARG_1_AND_2_ARE_WORDS) p += 4; else p += 2;
                if (flags & WE_HAVE_A_SCALE)              p += 2;
                else if (flags & WE_HAVE_AN_X_AND_Y_SCALE) p += 4;
                else if (flags & WE_HAVE_A_TWO_BY_TWO)    p += 8;

                if (comp_gid < gs->cap && !gs->used[comp_gid]) {
                    gs->used[comp_gid] = true;
                    changed = true;
                }
            } while (flags & MORE_COMPONENTS);
        }
    }
}

static uint32_t table_checksum(const uint8_t *data, uint32_t len) {
    uint32_t sum = 0;
    uint32_t nlong = (len + 3) / 4;
    for (uint32_t i = 0; i < nlong; i++) {
        uint32_t v = 0;
        for (int j = 0; j < 4; j++) {
            v <<= 8;
            if (i * 4 + j < len) v |= data[i * 4 + j];
        }
        sum += v;
    }
    return sum;
}

typedef struct { uint8_t *data; size_t len; size_t cap; } Buf;
static void buf_init(Buf *b) { b->data = NULL; b->len = 0; b->cap = 0; }
static void buf_ensure(Buf *b, size_t need) {
    if (b->len + need > b->cap) {
        b->cap = (b->len + need) * 2 + 64;
        b->data = (uint8_t*)realloc(b->data, b->cap);
    }
}
static void buf_append(Buf *b, const void *p, size_t n) {
    buf_ensure(b, n);
    memcpy(b->data + b->len, p, n);
    b->len += n;
}
static void buf_pad4(Buf *b) {
    while (b->len & 3) { buf_ensure(b, 1); b->data[b->len++] = 0; }
}

uint8_t *pt_font_subset(const pt_font *font,
                        const pt_glyph_set *gs,
                        size_t *out_size) {
    if (!font || !gs || font->num_glyphs == 0 || !out_size) return NULL;
    *out_size = 0;

    int used_count = 0;
    for (uint16_t g = 0; g < gs->cap; g++) {
        if (gs->used[g]) used_count++;
    }
    if (used_count == 0) return NULL;

    uint16_t total_glyphs = font->num_glyphs;

    Buf glyf_buf; buf_init(&glyf_buf);
    std::vector<uint32_t> new_loca(total_glyphs + 1, 0);

    for (uint16_t g = 0; g < total_glyphs; g++) {
        new_loca[g] = (uint32_t)glyf_buf.len;
        if (g < gs->cap && gs->used[g]) {
            uint32_t glen = loca_length(font, g);
            if (glen > 0 && font->off_glyf + loca_offset(font, g) + glen <= font->size) {
                buf_append(&glyf_buf, font->data + font->off_glyf + loca_offset(font, g), glen);
                buf_pad4(&glyf_buf);
            }
        }
    }
    new_loca[total_glyphs] = (uint32_t)glyf_buf.len;

    if (glyf_buf.len == 0) {
        free(glyf_buf.data);
        return NULL;
    }

    Buf loca_buf; buf_init(&loca_buf);
    for (uint16_t g = 0; g <= total_glyphs; g++) {
        uint8_t tmp[4];
        put32(tmp, new_loca[g]);
        buf_append(&loca_buf, tmp, 4);
    }

    Buf head_copy; buf_init(&head_copy);
    if (font->off_head && font->off_head + 54 <= font->size) {
        buf_append(&head_copy, font->data + font->off_head, 54);
        put16(head_copy.data + 50, 1);
        put32(head_copy.data + 8, 0);
    }

    typedef struct {
        uint32_t tag;
        const uint8_t *src;
        uint32_t src_len;
        Buf *dyn;
    } TableSlot;

    TableSlot slots[16];
    int nslots = 0;

    for (int i = 0; i < font->num_tables && nslots < 16; i++) {
        uint32_t tag = font->tables[i].tag;
        uint32_t off = font->tables[i].offset;
        uint32_t len = font->tables[i].length;

        if (tag == PT_TAG_glyf) {
            slots[nslots].tag = tag;
            slots[nslots].src = NULL;
            slots[nslots].src_len = 0;
            slots[nslots].dyn = &glyf_buf;
            nslots++;
        } else if (tag == PT_TAG_loca) {
            slots[nslots].tag = tag;
            slots[nslots].src = NULL;
            slots[nslots].src_len = 0;
            slots[nslots].dyn = &loca_buf;
            nslots++;
        } else if (tag == PT_TAG_head) {
            slots[nslots].tag = tag;
            slots[nslots].src = NULL;
            slots[nslots].src_len = 0;
            slots[nslots].dyn = &head_copy;
            nslots++;
        } else if (off + len <= font->size) {
            slots[nslots].tag = tag;
            slots[nslots].src = font->data + off;
            slots[nslots].src_len = len;
            slots[nslots].dyn = NULL;
            nslots++;
        }
    }

    size_t dir_size = 12 + nslots * 16;
    uint32_t running = (uint32_t)dir_size;

    typedef struct { uint32_t off; uint32_t len; uint32_t csum; } TInfo;
    std::vector<TInfo> tinfo(nslots);

    for (int i = 0; i < nslots; i++) {
        const uint8_t *d = slots[i].dyn ? slots[i].dyn->data : slots[i].src;
        uint32_t l       = slots[i].dyn ? (uint32_t)slots[i].dyn->len : slots[i].src_len;
        tinfo[i].off  = running;
        tinfo[i].len  = l;
        tinfo[i].csum = table_checksum(d, l);
        running += (l + 3) & ~3u;
    }

    size_t total_size = running;
    uint8_t *out = (uint8_t*)calloc(1, total_size);
    if (!out) {
        free(glyf_buf.data);
        free(loca_buf.data);
        free(head_copy.data);
        return NULL;
    }

    put32(out, 0x00010000);
    put16(out + 4, (uint16_t)nslots);
    uint16_t sr = 1; int es = 0;
    while (sr * 2 <= nslots) { sr *= 2; es++; }
    put16(out + 6, sr * 16);
    put16(out + 8, (uint16_t)es);
    put16(out + 10, (uint16_t)(nslots * 16 - sr * 16));

    for (int i = 0; i < nslots; i++) {
        uint8_t *e = out + 12 + i * 16;
        put32(e, slots[i].tag);
        put32(e + 4, tinfo[i].csum);
        put32(e + 8, tinfo[i].off);
        put32(e + 12, tinfo[i].len);
    }

    for (int i = 0; i < nslots; i++) {
        const uint8_t *d = slots[i].dyn ? slots[i].dyn->data : slots[i].src;
        uint32_t l       = slots[i].dyn ? (uint32_t)slots[i].dyn->len : slots[i].src_len;
        if (d && l > 0) memcpy(out + tinfo[i].off, d, l);
    }

    uint32_t file_csum = table_checksum(out, (uint32_t)total_size);
    uint32_t adj = 0xB1B0AFBA - file_csum;
    for (int i = 0; i < nslots; i++) {
        if (slots[i].tag == PT_TAG_head) {
            put32(out + tinfo[i].off + 8, adj);
            break;
        }
    }

    free(glyf_buf.data);
    free(loca_buf.data);
    free(head_copy.data);

    *out_size = total_size;
    return out;
}

static inline uint32_t cff_read_offset(const uint8_t *ptr, uint8_t off_size) {
    uint32_t val = 0;
    for (int i = 0; i < off_size; i++) val = (val << 8) | ptr[i];
    return val;
}

static inline void cff_write_offset(uint8_t *ptr, uint8_t off_size, uint32_t val) {
    for (int i = off_size - 1; i >= 0; i--) {
        ptr[i] = (uint8_t)(val & 0xFF);
        val >>= 8;
    }
}

static bool cff_parse_index(const uint8_t *p, size_t max_len,
                            uint16_t *out_count, uint8_t *out_off_size,
                            const uint8_t **out_offsets, const uint8_t **out_data,
                            size_t *out_index_size) {
    if (max_len < 2) return false;
    uint16_t count = be16(p);
    *out_count = count;
    if (count == 0) {
        *out_off_size = 0;
        *out_offsets = NULL;
        *out_data = NULL;
        *out_index_size = 2;
        return true;
    }
    if (max_len < 3) return false;
    uint8_t off_size = p[2];
    if (off_size < 1 || off_size > 4) return false;
    *out_off_size = off_size;
    size_t off_table_len = (size_t)(count + 1) * off_size;
    if (max_len < 3 + off_table_len) return false;
    *out_offsets = p + 3;

    uint32_t last_offset = cff_read_offset(*out_offsets + count * off_size, off_size);
    if (last_offset < 1) return false;
    uint32_t total_data = last_offset - 1;
    *out_data = p + 3 + off_table_len;
    *out_index_size = 3 + off_table_len + total_data;
    if (max_len < *out_index_size) return false;
    return true;
}

uint8_t *pt_cff_subset(const uint8_t *data, size_t size,
                       const pt_glyph_set *gs, size_t *out_size) {
    if (!data || size < 4 || !out_size) return NULL;
    *out_size = 0;
    if (data[0] != 1 || data[1] != 0) return NULL;

    uint8_t hdr_size = data[2];
    if (hdr_size < 4 || hdr_size >= size) return NULL;

    size_t pos = hdr_size;

    uint16_t name_count = 0;
    uint8_t name_off_size = 0;
    const uint8_t *name_offsets = NULL, *name_data = NULL;
    size_t name_idx_size = 0;
    if (!cff_parse_index(data + pos, size - pos, &name_count, &name_off_size,
                         &name_offsets, &name_data, &name_idx_size)) return NULL;
    pos += name_idx_size;

    uint16_t td_count = 0;
    uint8_t td_off_size = 0;
    const uint8_t *td_offsets = NULL, *td_data = NULL;
    size_t td_idx_size = 0;
    if (!cff_parse_index(data + pos, size - pos, &td_count, &td_off_size,
                         &td_offsets, &td_data, &td_idx_size)) return NULL;
    if (td_count == 0 || !td_data) return NULL;

    uint32_t charstrings_off = 0;
    size_t dpos = 0;
    size_t td_len = (size_t)cff_read_offset(td_offsets + td_off_size, td_off_size) - 1;
    std::vector<int32_t> operands;

    while (dpos < td_len) {
        uint8_t b0 = td_data[dpos];
        if (b0 >= 32 || b0 == 28 || b0 == 29) {
            int32_t val = 0;
            if (b0 >= 32 && b0 <= 246) {
                val = b0 - 139;
                dpos++;
            } else if (b0 >= 247 && b0 <= 250) {
                if (dpos + 1 >= td_len) break;
                val = (b0 - 247) * 256 + td_data[dpos + 1] + 108;
                dpos += 2;
            } else if (b0 >= 251 && b0 <= 254) {
                if (dpos + 1 >= td_len) break;
                val = -(int32_t)(b0 - 251) * 256 - td_data[dpos + 1] - 108;
                dpos += 2;
            } else if (b0 == 28) {
                if (dpos + 2 >= td_len) break;
                val = (int16_t)be16(td_data + dpos + 1);
                dpos += 3;
            } else if (b0 == 29) {
                if (dpos + 4 >= td_len) break;
                val = (int32_t)be32(td_data + dpos + 1);
                dpos += 5;
            }
            operands.push_back(val);
        } else if (b0 == 30) {
            dpos++;
            while (dpos < td_len) {
                uint8_t fb = td_data[dpos++];
                if ((fb & 0x0F) == 0x0F || (fb >> 4) == 0x0F) break;
            }
            operands.push_back(0);
        } else if (b0 <= 21) {
            if (b0 == 12) {
                dpos += 2;
            } else {
                if (b0 == 17 && !operands.empty()) {
                    charstrings_off = (uint32_t)operands.back();
                }
                dpos++;
            }
            operands.clear();
        } else {
            dpos++;
            operands.clear();
        }
    }

    if (charstrings_off == 0 || charstrings_off >= size) return NULL;

    uint16_t cs_count = 0;
    uint8_t cs_off_size = 0;
    const uint8_t *cs_offsets = NULL, *cs_data = NULL;
    size_t cs_idx_size = 0;
    if (!cff_parse_index(data + charstrings_off, size - charstrings_off,
                         &cs_count, &cs_off_size, &cs_offsets, &cs_data, &cs_idx_size)) {
        return NULL;
    }
    if (cs_count == 0 || cs_count > 65535) return NULL;
    if (charstrings_off + cs_idx_size > size) return NULL;

    std::vector<uint32_t> new_cs_offsets;
    new_cs_offsets.reserve(cs_count + 1);
    std::vector<uint8_t> new_cs_data;

    new_cs_offsets.push_back(1);
    for (uint16_t i = 0; i < cs_count; i++) {
        uint32_t o1 = cff_read_offset(cs_offsets + i * cs_off_size, cs_off_size);
        uint32_t o2 = cff_read_offset(cs_offsets + (i + 1) * cs_off_size, cs_off_size);
        uint32_t glyph_len = (o2 > o1) ? (o2 - o1) : 0;
        const uint8_t *g_bytes = cs_data + (o1 - 1);

        bool keep = (i == 0) || (gs && i < gs->cap && gs->used[i]);
        if (keep && glyph_len > 0) {
            new_cs_data.insert(new_cs_data.end(), g_bytes, g_bytes + glyph_len);
        } else {
            new_cs_data.push_back(0x0E);
        }
        new_cs_offsets.push_back((uint32_t)new_cs_data.size() + 1);
    }

    uint32_t total_cs_data = (uint32_t)new_cs_data.size();
    uint8_t new_cs_off_size = 1;
    if (total_cs_data + 1 > 0xFFFFFF) new_cs_off_size = 4;
    else if (total_cs_data + 1 > 0xFFFF) new_cs_off_size = 3;
    else if (total_cs_data + 1 > 0xFF) new_cs_off_size = 2;

    std::vector<uint8_t> new_cs_index;
    new_cs_index.resize(3 + (size_t)(cs_count + 1) * new_cs_off_size + total_cs_data);
    put16(new_cs_index.data(), cs_count);
    new_cs_index[2] = new_cs_off_size;

    for (size_t i = 0; i <= cs_count; i++) {
        cff_write_offset(new_cs_index.data() + 3 + i * new_cs_off_size,
                         new_cs_off_size, new_cs_offsets[i]);
    }
    memcpy(new_cs_index.data() + 3 + (size_t)(cs_count + 1) * new_cs_off_size,
           new_cs_data.data(), total_cs_data);

    if (new_cs_index.size() >= cs_idx_size) return NULL;

    std::vector<uint8_t> out_buf(data, data + size);
    memcpy(out_buf.data() + charstrings_off, new_cs_index.data(), new_cs_index.size());
    size_t saved_bytes = cs_idx_size - new_cs_index.size();

    size_t rest_start = charstrings_off + cs_idx_size;
    if (rest_start < size) {
        memmove(out_buf.data() + charstrings_off + new_cs_index.size(),
                out_buf.data() + rest_start,
                size - rest_start);
    }
    out_buf.resize(size - saved_bytes);

    uint8_t *result = (uint8_t*)malloc(out_buf.size());
    if (!result) return NULL;
    memcpy(result, out_buf.data(), out_buf.size());
    *out_size = out_buf.size();
    return result;
}
