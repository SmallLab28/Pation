/*
    font.h — Pation Font Subsetting Subsystem
    Strips a TrueType / OpenType font down to only the glyphs that are
    actually referenced in the PDF content stream, producing the smallest
    possible embedded font program.

    Pipeline:
      1. Parse the font binary (sfnt container) — locate required tables.
      2. Walk the cmap → build a glyph-ID set from used codepoints.
      3. Walk glyf / loca — recursively add composite glyph dependencies.
      4. Rebuild the subset font: keep only needed tables / glyph data.
      5. Return a malloc'd buffer the writer can embed as a /FontFile2 stream.

    SMALLAB28 SOFTWARE — ALL RIGHTS RESERVED
*/
#ifndef FONT_H
#define FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  Four-character table tags (big-endian uint32)                      */
/* ------------------------------------------------------------------ */
#define PT_TAG(a,b,c,d) (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)| \
                          ((uint32_t)(c)<< 8)|((uint32_t)(d)))

#define PT_TAG_head  PT_TAG('h','e','a','d')
#define PT_TAG_hhea  PT_TAG('h','h','e','a')
#define PT_TAG_maxp  PT_TAG('m','a','x','p')
#define PT_TAG_OS2   PT_TAG('O','S','/','2')
#define PT_TAG_name  PT_TAG('n','a','m','e')
#define PT_TAG_cmap  PT_TAG('c','m','a','p')
#define PT_TAG_post  PT_TAG('p','o','s','t')
#define PT_TAG_hmtx  PT_TAG('h','m','t','x')
#define PT_TAG_loca  PT_TAG('l','o','c','a')
#define PT_TAG_glyf  PT_TAG('g','l','y','f')
#define PT_TAG_cvt   PT_TAG('c','v','t',' ')
#define PT_TAG_fpgm  PT_TAG('f','p','g','m')
#define PT_TAG_prep  PT_TAG('p','r','e','p')
#define PT_TAG_CFF   PT_TAG('C','F','F',' ')
#define PT_TAG_CFF2  PT_TAG('C','F','F','2')

/* ------------------------------------------------------------------ */
/*  sfnt table-directory entry                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t tag;
    uint32_t checksum;
    uint32_t offset;
    uint32_t length;
} pt_sfnt_table;

/* ------------------------------------------------------------------ */
/*  Font object (parsed in-memory view)                                */
/* ------------------------------------------------------------------ */
typedef struct {
    const uint8_t *data;        /* raw font file bytes (not owned)  */
    size_t         size;        /* byte count                        */
    uint16_t       num_tables;
    pt_sfnt_table *tables;      /* heap-alloc'd, num_tables entries  */

    /* cached table locations */
    uint32_t off_head, off_hhea, off_maxp;
    uint32_t off_cmap, off_loca, off_glyf;
    uint32_t off_hmtx, off_OS2,  off_name;
    uint32_t off_post;
    uint32_t off_cvt,  len_cvt;
    uint32_t off_fpgm, len_fpgm;
    uint32_t off_prep, len_prep;

    uint16_t num_glyphs;
    uint16_t index_to_loc_format; /* 0 = short, 1 = long */
    uint16_t num_h_metrics;
} pt_font;

/* ------------------------------------------------------------------ */
/*  Glyph-usage set                                                    */
/* ------------------------------------------------------------------ */
typedef struct {
    bool    *used;         /* used[gid] = true if glyph needed    */
    uint16_t cap;          /* capacity == num_glyphs               */
} pt_glyph_set;

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

/*
 * pt_font_open
 *   Parse font binary (does NOT copy data — caller keeps it alive).
 *   Returns 0 on success, -1 on error.
 */
int pt_font_open(pt_font *font, const uint8_t *data, size_t size);

/*
 * pt_font_close
 *   Release any heap memory owned by pt_font.
 */
void pt_font_close(pt_font *font);

/*
 * pt_glyph_set_create / pt_glyph_set_free
 */
pt_glyph_set pt_glyph_set_create(uint16_t num_glyphs);
void         pt_glyph_set_free  (pt_glyph_set *gs);

/*
 * pt_cmap_mark
 *   For each Unicode codepoint in codepoints[], look up the glyph ID
 *   via the font's cmap and mark it used in gs.
 *   Also marks GID 0 (the .notdef glyph) always.
 */
void pt_cmap_mark(const pt_font *font,
                  pt_glyph_set  *gs,
                  const uint32_t *codepoints,
                  size_t          count);

/*
 * pt_glyf_closure
 *   Walk composite glyph records recursively and add any referenced
 *   component GIDs to gs.
 */
void pt_glyf_closure(const pt_font *font, pt_glyph_set *gs);

/*
 * pt_font_subset
 *   Build a new, complete sfnt font containing only the glyphs in gs.
 *   Returns a malloc'd buffer + its byte length.
 *   Caller must free() the returned pointer.
 */
uint8_t *pt_font_subset(const pt_font    *font,
                        const pt_glyph_set *gs,
                        size_t            *out_size);

uint8_t *pt_cff_subset(const uint8_t *data, size_t size,
                       const pt_glyph_set *gs, size_t *out_size);

#ifdef __cplusplus
}
#endif
#endif /* FONT_H */
