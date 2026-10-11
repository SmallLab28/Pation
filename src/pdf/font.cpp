/*
    font.cpp — TrueType/OpenType font subsetting from scratch
    Strips a TrueType font down to only the glyphs that are used in
    a PDF, producing the smallest possible valid sfnt binary.

    Supported input: TrueType (glyf/loca) — the most common PDF
    embedded font type.  CFF-based fonts (OTF CFF) are a future todo.

    Algorithm:
      1. Parse the sfnt table directory.
      2. Map used Unicode codepoints → GIDs via cmap format-4.
      3. Add .notdef (GID 0) unconditionally.
      4. For composite glyphs, recursively add component GIDs.
      5. Build a GID remapping table (old_gid → new_gid, in order).
      6. Rebuild:  glyf — copy only used glyph data, fix component refs.
                  loca — new offsets.
                  hmtx — keep only used entries.
                  maxp — update num_glyphs.
                  head — fix checkSumAdjustment, indexToLocFormat.
                  cmap — emit a minimal format-4 covering used codepoints.
                  hhea — update num_h_metrics.
                  Keep: name, post, OS/2, cvt, fpgm, prep as-is.
      7. Write the new sfnt table directory + tables.

    SMALLAB28 SOFTWARE — ALL RIGHTS RESERVED
*/
#include "pdf/font.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* ================================================================== */
/*  Endian helpers (font files are big-endian)                        */
/* ================================================================== */
static inline uint16_t be16(const uint8_t *p) {
    return (uint16_t)((p[0]<<8)|p[1]);
}
static inline uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
           ((uint32_t)p[2]<<8)|(uint32_t)p[3];
}
static inline void put16(uint8_t *p, uint16_t v) {
    p[0]=(v>>8)&0xFF; p[1]=v&0xFF;
}
static inline void put32(uint8_t *p, uint32_t v) {
    p[0]=(v>>24)&0xFF; p[1]=(v>>16)&0xFF;
    p[2]=(v>>8)&0xFF;  p[3]=v&0xFF;
}

/* ================================================================== */
/*  Open / close                                                       */
/* ================================================================== */
int pt_font_open(pt_font *font, const uint8_t *data, size_t size) {
    memset(font, 0, sizeof(*font));
    if (size < 12) return -1;
    font->data = data;
    font->size = size;

    uint32_t sfver = be32(data);
    /* accept 0x00010000 (TrueType) or 'true' or 'OTTO' (CFF) */
    if (sfver != 0x00010000 && sfver != PT_TAG('t','r','u','e') &&
        sfver != PT_TAG('O','T','T','O')) {
        fprintf(stderr, "font.cpp: unrecognized sfnt version 0x%08X\n", sfver);
        return -1;
    }

    font->num_tables = be16(data+4);
    font->tables = (pt_sfnt_table*)calloc(font->num_tables, sizeof(pt_sfnt_table));
    if (!font->tables) return -1;

    const uint8_t *entry = data + 12;
    for (int i = 0; i < font->num_tables; i++, entry += 16) {
        font->tables[i].tag      = be32(entry);
        font->tables[i].checksum = be32(entry+4);
        font->tables[i].offset   = be32(entry+8);
        font->tables[i].length   = be32(entry+12);

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

    /* Read num_glyphs from maxp */
    if (font->off_maxp && font->off_maxp + 6 <= size)
        font->num_glyphs = be16(data + font->off_maxp + 4);

    /* Read indexToLocFormat from head */
    if (font->off_head && font->off_head + 52 <= size)
        font->index_to_loc_format = be16(data + font->off_head + 50);

    /* Read num_h_metrics from hhea */
    if (font->off_hhea && font->off_hhea + 36 <= size)
        font->num_h_metrics = be16(data + font->off_hhea + 34);

    return 0;
}

void pt_font_close(pt_font *font) {
    free(font->tables);
    memset(font, 0, sizeof(*font));
}

/* ================================================================== */
/*  Glyph set                                                          */
/* ================================================================== */
pt_glyph_set pt_glyph_set_create(uint16_t num_glyphs) {
    pt_glyph_set gs;
    gs.cap  = num_glyphs;
    gs.used = (bool*)calloc(num_glyphs, sizeof(bool));
    return gs;
}
void pt_glyph_set_free(pt_glyph_set *gs) {
    free(gs->used); gs->used = NULL;
}

/* ================================================================== */
/*  cmap format-4 lookup                                              */
/* ================================================================== */
static uint16_t cmap_lookup_f4(const uint8_t *seg, uint16_t codepoint,
                                int seg_count) {
    /*
     * seg points to the start of the format-4 subtable (the 'format' word).
     * Layout: format(2) length(2) language(2) segCount×2(2) ...
     *         endCodes[segCount×2] reservedPad(2) startCodes[…]
     *         idDelta[…] idRangeOffset[…] glyphIds[…]
     */
    /* segCountX2 is at offset 6 */
    uint16_t segCountX2 = be16(seg+6);
    int sc = segCountX2/2;
    const uint8_t *end_code    = seg + 14;              /* endCode[sc]    */
    const uint8_t *start_code  = end_code + sc*2 + 2;  /* +pad=2         */
    const uint8_t *id_delta    = start_code + sc*2;
    const uint8_t *id_range_off= id_delta   + sc*2;
    const uint8_t *glyph_ids   = id_range_off + sc*2;

    for (int i = 0; i < sc; i++) {
        uint16_t end   = be16(end_code   + i*2);
        uint16_t start = be16(start_code + i*2);
        if (codepoint > end) continue;
        if (codepoint < start) return 0;
        uint16_t delta  = be16(id_delta    + i*2);
        uint16_t roff   = be16(id_range_off + i*2);
        if (roff == 0) {
            return (uint16_t)((codepoint + delta) & 0xFFFF);
        } else {
            /* idRangeOffset != 0 */
            const uint8_t *p = id_range_off + i*2 + roff + (codepoint - start)*2;
            uint16_t gid = be16(p);
            if (gid == 0) return 0;
            return (uint16_t)((gid + delta) & 0xFFFF);
        }
    }
    return 0;
}

void pt_cmap_mark(const pt_font *font, pt_glyph_set *gs,
                  const uint32_t *codepoints, size_t count) {
    /* Mark .notdef always */
    if (gs->cap > 0) gs->used[0] = true;

    if (!font->off_cmap) return;
    const uint8_t *cmap = font->data + font->off_cmap;
    uint16_t n_subtables = be16(cmap+2);

    /* Find first platform 3 (Windows) encoding-id 1 (BMP Unicode), or platform 0 */
    const uint8_t *chosen = NULL;
    for (int i = 0; i < n_subtables; i++) {
        const uint8_t *rec = cmap + 4 + i*8;
        uint16_t plat = be16(rec);
        uint16_t enc  = be16(rec+2);
        uint32_t off  = be32(rec+4);
        const uint8_t *sub = cmap + off;
        uint16_t fmt = be16(sub);
        if (fmt == 4 && ((plat==3&&enc==1)||(plat==0))) {
            chosen = sub;
            if (plat==3&&enc==1) break; /* prefer Windows BMP */
        }
    }
    if (!chosen) return;

    for (size_t k = 0; k < count; k++) {
        if (codepoints[k] > 0xFFFF) continue; /* BMP only for now */
        uint16_t gid = cmap_lookup_f4(chosen, (uint16_t)codepoints[k],
                                      be16(chosen+6)/2);
        if (gid > 0 && gid < gs->cap)
            gs->used[gid] = true;
    }
}

/* ================================================================== */
/*  loca helpers                                                       */
/* ================================================================== */
static uint32_t loca_offset(const pt_font *font, uint16_t gid) {
    if (font->index_to_loc_format == 0)
        return (uint32_t)be16(font->data + font->off_loca + gid*2) * 2;
    return be32(font->data + font->off_loca + gid*4);
}
static uint32_t loca_length(const pt_font *font, uint16_t gid) {
    uint32_t a = loca_offset(font, gid);
    uint32_t b = loca_offset(font, gid+1);
    return (b > a) ? (b-a) : 0;
}

/* ================================================================== */
/*  Composite glyph closure                                           */
/* ================================================================== */
/* Composite flags */
#define ARG_1_AND_2_ARE_WORDS  (1<<0)
#define MORE_COMPONENTS        (1<<5)
#define WE_HAVE_A_SCALE        (1<<3)
#define WE_HAVE_AN_X_AND_Y_SCALE (1<<6)
#define WE_HAVE_A_TWO_BY_TWO   (1<<7)

void pt_glyf_closure(const pt_font *font, pt_glyph_set *gs) {
    /* iterate until no new glyphs are added */
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
            if (n_contours >= 0) continue; /* simple glyph */

            /* composite: parse components */
            const uint8_t *p = g + 10;
            uint16_t flags;
            do {
                flags        = be16(p); p += 2;
                uint16_t comp_gid = be16(p); p += 2;
                /* skip args */
                if (flags & ARG_1_AND_2_ARE_WORDS) p += 4; else p += 2;
                /* skip transform */
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

/* ================================================================== */
/*  Rebuild a cmap format-4 covering a list of (unicode, newGID) pairs*/
/* ================================================================== */
typedef struct { uint16_t unicode; uint16_t new_gid; } CmapPair;

static int cmpPair(const void *a, const void *b) {
    return (int)((CmapPair*)a)->unicode - (int)((CmapPair*)b)->unicode;
}

static size_t build_cmap4(const CmapPair *pairs, int n,
                           uint8_t **out) {
    /* Build contiguous segments */
    typedef struct { uint16_t start, end, new_start; } Seg;
    Seg *segs = (Seg*)malloc((n+1)*sizeof(Seg));
    int ns = 0;
    for (int i = 0; i < n; i++) {
        if (ns == 0 ||
            pairs[i].unicode != segs[ns-1].end+1 ||
            pairs[i].new_gid != segs[ns-1].new_start + (segs[ns-1].end - segs[ns-1].start) + 1) {
            segs[ns].start     = pairs[i].unicode;
            segs[ns].end       = pairs[i].unicode;
            segs[ns].new_start = pairs[i].new_gid;
            ns++;
        } else {
            segs[ns-1].end = pairs[i].unicode;
        }
    }
    /* Terminator segment 0xFFFF */
    segs[ns].start = segs[ns].end = 0xFFFF;
    segs[ns].new_start = 1;
    ns++;

    uint16_t segCount = (uint16_t)ns;
    /* Format-4 size: 14 + 8*segCount bytes (no glyphIdArray needed with delta) */
    size_t sz = 14 + 4*segCount*2 + 2; /* +2 for reservedPad */
    uint8_t *buf = (uint8_t*)calloc(1, sz);

    put16(buf+0,  4);                   /* format */
    put16(buf+2,  (uint16_t)sz);        /* length */
    put16(buf+4,  0);                   /* language */
    put16(buf+6,  segCount*2);          /* segCountX2 */

    /* searchRange, entrySelector, rangeShift */
    uint16_t sr = 1; int es = 0;
    while (sr*2 <= segCount) { sr*=2; es++; }
    put16(buf+8,  sr*2);
    put16(buf+10, (uint16_t)es);
    put16(buf+12, (uint16_t)(segCount*2 - sr*2));

    uint8_t *end_codes   = buf + 14;
    uint8_t *pad         = end_codes + segCount*2;
    uint8_t *start_codes = pad + 2;
    uint8_t *deltas      = start_codes + segCount*2;
    uint8_t *range_offs  = deltas + segCount*2;

    for (int i = 0; i < ns; i++) {
        put16(end_codes   + i*2, segs[i].end);
        put16(start_codes + i*2, segs[i].start);
        /* delta = new_start - start */
        uint16_t delta = (uint16_t)((int)segs[i].new_start - (int)segs[i].start);
        put16(deltas    + i*2, delta);
        put16(range_offs+ i*2, 0);    /* use delta form */
    }
    put16(pad, 0); /* reservedPad */

    free(segs);
    *out = buf;
    return sz;
}

/* ================================================================== */
/*  Checksum                                                           */
/* ================================================================== */
static uint32_t table_checksum(const uint8_t *data, uint32_t len) {
    uint32_t sum = 0;
    uint32_t nlong = (len+3)/4;
    for (uint32_t i = 0; i < nlong; i++) {
        uint32_t v = 0;
        for (int j = 0; j < 4; j++) {
            v <<= 8;
            if (i*4+j < len) v |= data[i*4+j];
        }
        sum += v;
    }
    return sum;
}

/* ================================================================== */
/*  Dynamic byte buffer                                                */
/* ================================================================== */
typedef struct { uint8_t *data; size_t len; size_t cap; } Buf;

static void buf_init(Buf *b) { b->data=NULL; b->len=0; b->cap=0; }
static void buf_ensure(Buf *b, size_t need) {
    if (b->len+need > b->cap) {
        b->cap = (b->len+need)*2 + 64;
        b->data = (uint8_t*)realloc(b->data, b->cap);
    }
}
static void buf_append(Buf *b, const void *p, size_t n) {
    buf_ensure(b, n);
    memcpy(b->data+b->len, p, n);
    b->len += n;
}
static void buf_pad4(Buf *b) {
    while (b->len & 3) { buf_ensure(b, 1); b->data[b->len++]=0; }
}

/* ================================================================== */
/*  Main subset builder                                                */
/* ================================================================== */
uint8_t *pt_font_subset(const pt_font *font,
                        const pt_glyph_set *gs,
                        size_t *out_size) {
    /* --- 1. Count and number new glyphs --- */
    int new_glyph_count = 0;
    uint16_t *old_to_new = (uint16_t*)malloc(gs->cap * sizeof(uint16_t));
    uint16_t *new_to_old = (uint16_t*)malloc(gs->cap * sizeof(uint16_t));
    memset(old_to_new, 0xFF, gs->cap * sizeof(uint16_t));

    for (uint16_t g = 0; g < gs->cap; g++) {
        if (gs->used[g]) {
            old_to_new[g] = (uint16_t)new_glyph_count;
            new_to_old[new_glyph_count] = g;
            new_glyph_count++;
        }
    }

    /* --- 2. Build new glyf + loca --- */
    Buf glyf_buf; buf_init(&glyf_buf);
    /* loca: long format, new_glyph_count+1 entries */
    uint32_t *new_loca = (uint32_t*)calloc(new_glyph_count+1, sizeof(uint32_t));

    for (int ni = 0; ni < new_glyph_count; ni++) {
        uint16_t old_gid = new_to_old[ni];
        new_loca[ni] = (uint32_t)glyf_buf.len;

        uint32_t glen = loca_length(font, old_gid);
        if (glen == 0) continue;

        const uint8_t *gsrc = font->data + font->off_glyf + loca_offset(font, old_gid);
        /* Copy glyph data; if composite, fix component GIDs */
        int16_t n_contours = (int16_t)be16(gsrc);
        if (n_contours < 0) {
            /* composite — need to remap component GIDs */
            uint8_t *gcopy = (uint8_t*)malloc(glen);
            memcpy(gcopy, gsrc, glen);
            uint8_t *p = gcopy + 10;
            uint16_t flags;
            do {
                flags = be16(p); p += 2;
                uint16_t cg = be16(p);
                if (cg < gs->cap && old_to_new[cg] != 0xFFFF)
                    put16(p, old_to_new[cg]);
                p += 2;
                if (flags & ARG_1_AND_2_ARE_WORDS) p += 4; else p += 2;
                if (flags & WE_HAVE_A_SCALE)               p += 2;
                else if (flags & WE_HAVE_AN_X_AND_Y_SCALE) p += 4;
                else if (flags & WE_HAVE_A_TWO_BY_TWO)     p += 8;
            } while (flags & MORE_COMPONENTS);
            buf_append(&glyf_buf, gcopy, glen);
            free(gcopy);
        } else {
            buf_append(&glyf_buf, gsrc, glen);
        }
        buf_pad4(&glyf_buf);
    }
    new_loca[new_glyph_count] = (uint32_t)glyf_buf.len;

    /* --- 3. Build new hmtx --- */
    /* hMetrics[num_h_metrics]{advanceWidth, lsb} + lsb-only[] */
    Buf hmtx_buf; buf_init(&hmtx_buf);
    for (int ni = 0; ni < new_glyph_count; ni++) {
        uint16_t old_gid = new_to_old[ni];
        uint32_t hoff;
        uint16_t aw=0, lsb=0;
        if (old_gid < font->num_h_metrics) {
            hoff = font->off_hmtx + old_gid*4;
            aw  = be16(font->data + hoff);
            lsb = be16(font->data + hoff + 2);
        } else {
            /* lsb only metrics */
            hoff = font->off_hmtx + (font->num_h_metrics-1)*4;
            aw  = be16(font->data + hoff);
            uint32_t loff = font->off_hmtx + font->num_h_metrics*4
                            + (old_gid - font->num_h_metrics)*2;
            lsb = be16(font->data + loff);
        }
        uint8_t entry[4]; put16(entry, aw); put16(entry+2, lsb);
        buf_append(&hmtx_buf, entry, 4);
    }

    /* --- 4. Build new cmap --- */
    /* Collect (unicode, new_gid) pairs from the old cmap */
    /* Simple approach: re-scan cmap format-4 for all used old GIDs */
    CmapPair *pairs = (CmapPair*)malloc(new_glyph_count * sizeof(CmapPair));
    int npairs = 0;

    if (font->off_cmap) {
        const uint8_t *cmap = font->data + font->off_cmap;
        uint16_t n_subs = be16(cmap+2);
        const uint8_t *chosen = NULL;
        for (int i = 0; i < n_subs; i++) {
            const uint8_t *rec = cmap + 4 + i*8;
            uint16_t plat = be16(rec), enc = be16(rec+2);
            const uint8_t *sub = cmap + be32(rec+4);
            if (be16(sub)==4 && ((plat==3&&enc==1)||(plat==0)))
                { chosen=sub; if(plat==3&&enc==1) break; }
        }
        if (chosen) {
            /* Enumerate codepoints 0x0020-0xFFFF */
            uint16_t segCountX2 = be16(chosen+6);
            int sc = segCountX2/2;
            const uint8_t *end_code = chosen+14;
            const uint8_t *start_code = end_code + sc*2 + 2;
            const uint8_t *id_delta   = start_code + sc*2;
            const uint8_t *id_roff    = id_delta + sc*2;
            for (int i = 0; i < sc; i++) {
                uint16_t es = be16(start_code+i*2);
                uint16_t ee = be16(end_code+i*2);
                for (uint16_t cp = es; cp <= ee && cp != 0xFFFF; cp++) {
                    uint16_t gid = cmap_lookup_f4(chosen, cp, sc);
                    if (gid < gs->cap && gs->used[gid] && npairs < new_glyph_count) {
                        pairs[npairs].unicode  = cp;
                        pairs[npairs].new_gid  = old_to_new[gid];
                        npairs++;
                    }
                }
                if (ee == 0xFFFF) break;
            }
        }
    }
    qsort(pairs, npairs, sizeof(CmapPair), cmpPair);
    uint8_t *cmap4_data = NULL;
    size_t   cmap4_size = build_cmap4(pairs, npairs, &cmap4_data);
    free(pairs);

    /* Wrap format-4 in a full cmap table:
       cmap header(4) + 1 encoding record(8) + format-4 data */
    Buf cmap_buf; buf_init(&cmap_buf);
    {
        uint8_t hdr[4+8];
        put16(hdr+0, 0);   /* version */
        put16(hdr+2, 1);   /* numTables */
        put16(hdr+4, 3);   /* platformID: Windows */
        put16(hdr+6, 1);   /* encodingID: BMP Unicode */
        put32(hdr+8, 12);  /* offset to format-4 data */
        buf_append(&cmap_buf, hdr, 12);
    }
    buf_append(&cmap_buf, cmap4_data, cmap4_size);
    free(cmap4_data);

    /* --- 5. Decide which tables to include --- */
    /* Required: head, hhea, maxp, OS/2, name, cmap, post, hmtx, loca, glyf */
    /* Optional: cvt, fpgm, prep */
    typedef struct {
        uint32_t tag;
        const uint8_t *src;
        uint32_t      src_len;
        Buf           *dyn;   /* if non-NULL, use this instead */
    } TableSlot;

    /* Copy head as-is (we'll patch it later) */
    Buf head_copy; buf_init(&head_copy);
    if (font->off_head) {
        uint32_t hl = 54;  /* head table is always 54 bytes */
        buf_append(&head_copy, font->data+font->off_head, hl);
        /* indexToLocFormat → 1 (long) */
        put16(head_copy.data + 50, 1);
        /* clear checkSumAdjustment */
        put32(head_copy.data + 8, 0);
    }

    /* Update hhea: num_h_metrics = new_glyph_count */
    Buf hhea_copy; buf_init(&hhea_copy);
    if (font->off_hhea) {
        uint32_t hl = 36;
        buf_append(&hhea_copy, font->data+font->off_hhea, hl);
        put16(hhea_copy.data + 34, (uint16_t)new_glyph_count);
    }

    /* Update maxp: num_glyphs */
    Buf maxp_copy; buf_init(&maxp_copy);
    if (font->off_maxp) {
        uint32_t ml = 32; /* version 1.0 */
        uint32_t actual = 6; /* at minimum */
        /* find real length */
        for (int i=0;i<font->num_tables;i++) {
            if(font->tables[i].tag==PT_TAG_maxp) { ml=font->tables[i].length; break; }
        }
        buf_append(&maxp_copy, font->data+font->off_maxp, ml);
        put16(maxp_copy.data + 4, (uint16_t)new_glyph_count);
    }

    /* loca buf */
    Buf loca_buf; buf_init(&loca_buf);
    for (int i = 0; i <= new_glyph_count; i++) {
        uint8_t tmp[4]; put32(tmp, new_loca[i]);
        buf_append(&loca_buf, tmp, 4);
    }
    free(new_loca);

#define MAX_TABLES 16
    TableSlot slots[MAX_TABLES];
    int nslots = 0;

#define ADD_STATIC(t, off_field, len_field)                         \
    if (font->off_##off_field) {                                    \
        uint32_t _l = 0;                                            \
        for (int _i=0;_i<font->num_tables;_i++)                     \
            if(font->tables[_i].tag==t) { _l=font->tables[_i].length; break; } \
        slots[nslots].tag     = t;                                  \
        slots[nslots].src     = font->data+font->off_##off_field;   \
        slots[nslots].src_len = _l;                                 \
        slots[nslots].dyn     = NULL;                               \
        nslots++;                                                    \
    }
#define ADD_DYN(t, buf_ptr)                                         \
    if ((buf_ptr)->len > 0) {                                       \
        slots[nslots].tag     = t;                                  \
        slots[nslots].src     = NULL;                               \
        slots[nslots].src_len = 0;                                  \
        slots[nslots].dyn     = (buf_ptr);                          \
        nslots++;                                                    \
    }

    /* Order matters for some readers; use canonical order */
    ADD_DYN(PT_TAG_head, &head_copy);
    ADD_DYN(PT_TAG_hhea, &hhea_copy);
    ADD_DYN(PT_TAG_maxp, &maxp_copy);
    ADD_STATIC(PT_TAG_OS2,  OS2,  0);
    ADD_STATIC(PT_TAG_name, name, 0);
    ADD_DYN(PT_TAG_cmap,  &cmap_buf);
    ADD_STATIC(PT_TAG_post, post, 0);
    ADD_DYN(PT_TAG_hmtx,  &hmtx_buf);
    if (font->off_cvt  && font->len_cvt)  ADD_STATIC(PT_TAG_cvt,  cvt,  len_cvt);
    if (font->off_fpgm && font->len_fpgm) ADD_STATIC(PT_TAG_fpgm, fpgm, len_fpgm);
    if (font->off_prep && font->len_prep) ADD_STATIC(PT_TAG_prep, prep, len_prep);
    ADD_DYN(PT_TAG_loca,  &loca_buf);
    ADD_DYN(PT_TAG_glyf,  &glyf_buf);

    /* --- 6. Write sfnt --- */
    /* sfnt offset table: 12 bytes */
    /* table directory: nslots × 16 bytes */
    /* then table data (padded to 4) */
    size_t dir_size = 12 + nslots*16;
    uint32_t data_offset = (uint32_t)dir_size;

    /* Pre-compute offsets and checksums */
    typedef struct { uint32_t off; uint32_t len; uint32_t csum; } TInfo;
    TInfo *tinfo = (TInfo*)calloc(nslots, sizeof(TInfo));
    uint32_t running = data_offset;
    for (int i = 0; i < nslots; i++) {
        const uint8_t *d = slots[i].dyn ? slots[i].dyn->data : slots[i].src;
        uint32_t       l = slots[i].dyn ? (uint32_t)slots[i].dyn->len : slots[i].src_len;
        tinfo[i].off  = running;
        tinfo[i].len  = l;
        tinfo[i].csum = table_checksum(d, l);
        running += (l+3)&~3u;
    }

    size_t total_size = running;
    uint8_t *out = (uint8_t*)calloc(1, total_size);

    /* sfnt offset table */
    put32(out, 0x00010000); /* sfVersion TrueType */
    put16(out+4, (uint16_t)nslots);
    /* searchRange, entrySelector, rangeShift */
    uint16_t sr=1; int es=0;
    while (sr*2 <= nslots) { sr*=2; es++; }
    put16(out+6,  sr*16);
    put16(out+8,  (uint16_t)es);
    put16(out+10, (uint16_t)(nslots*16 - sr*16));

    /* Table directory */
    for (int i = 0; i < nslots; i++) {
        uint8_t *e = out + 12 + i*16;
        put32(e,   slots[i].tag);
        put32(e+4, tinfo[i].csum);
        put32(e+8, tinfo[i].off);
        put32(e+12,tinfo[i].len);
    }

    /* Table data */
    for (int i = 0; i < nslots; i++) {
        const uint8_t *d = slots[i].dyn ? slots[i].dyn->data : slots[i].src;
        uint32_t       l = slots[i].dyn ? (uint32_t)slots[i].dyn->len : slots[i].src_len;
        memcpy(out + tinfo[i].off, d, l);
    }

    /* Fix head.checkSumAdjustment = 0xB1B0AFBA - whole-file checksum */
    uint32_t file_csum = table_checksum(out, (uint32_t)total_size);
    uint32_t adj = 0xB1B0AFBA - file_csum;
    /* find head in output */
    for (int i = 0; i < nslots; i++) {
        if (slots[i].tag == PT_TAG_head) {
            put32(out + tinfo[i].off + 8, adj);
            break;
        }
    }

    /* Cleanup */
    free(tinfo);
    free(old_to_new);
    free(new_to_old);
    free(head_copy.data);
    free(hhea_copy.data);
    free(maxp_copy.data);
    free(cmap_buf.data);
    free(hmtx_buf.data);
    free(loca_buf.data);
    free(glyf_buf.data);

    *out_size = total_size;
    return out;
}
