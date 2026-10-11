/*
    dct.h — DCT (Discrete Cosine Transform) engine
    Full 8x8 block FDCT + quantisation + zigzag + Huffman entropy coding
    producing a minimal valid JFIF/JPEG bitstream — from scratch.
    SMALLAB28 SOFTWARE — ALL RIGHTS RESERVED
*/
#ifndef DCT_H
#define DCT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Standard JPEG luminance / chrominance quantisation tables          */
/* ------------------------------------------------------------------ */
extern const uint8_t pt_luma_qtable[64];
extern const uint8_t pt_chroma_qtable[64];

/* ------------------------------------------------------------------ */
/*  FDCT: 8x8 float block → quantised coefficients (zigzag order)     */
/* ------------------------------------------------------------------ */
void pt_fdct_block(const float in[64], float out[64]);
void pt_quantise  (const float dct[64], const uint8_t qtable[64],
                   int16_t coeffs[64]);

/* ------------------------------------------------------------------ */
/*  Zigzag scan index table (standard JPEG order)                      */
/* ------------------------------------------------------------------ */
extern const uint8_t pt_zigzag[64];

/* ------------------------------------------------------------------ */
/*  Huffman tables (DC + AC, luma + chroma) — standard baseline JPEG  */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t  lengths[17]; /* lengths[i] = # codes of length i  */
    uint8_t  values[256]; /* canonical Huffman values           */
    /* derived at runtime */
    uint16_t codes[256];
    uint8_t  code_len[256];
    int      count;
} pt_huffman_table;

void pt_huffman_build(pt_huffman_table *ht);

/* Standard luma/chroma DC/AC tables (globally available) */
extern pt_huffman_table pt_luma_dc_ht;
extern pt_huffman_table pt_luma_ac_ht;
extern pt_huffman_table pt_chroma_dc_ht;
extern pt_huffman_table pt_chroma_ac_ht;

void pt_huffman_tables_init(void);  /* call once before encoding */

/* ------------------------------------------------------------------ */
/*  Bit-stream writer                                                   */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   pos;     /* byte position                         */
    uint32_t bit_buf; /* pending bits                          */
    int      bit_cnt; /* how many valid bits in bit_buf        */
} pt_bitwriter;

void pt_bw_init   (pt_bitwriter *bw, size_t initial_cap);
void pt_bw_free   (pt_bitwriter *bw);
void pt_bw_write_bits(pt_bitwriter *bw, uint32_t bits, int n_bits);
void pt_bw_flush  (pt_bitwriter *bw);
void pt_bw_write_byte(pt_bitwriter *bw, uint8_t b);

/* ------------------------------------------------------------------ */
/*  DCT encode one component plane                                      */
/* ------------------------------------------------------------------ */
void pt_encode_plane(pt_bitwriter *bw,
                     const uint8_t *plane,
                     int width, int height,
                     const uint8_t qtable[64],
                     pt_huffman_table *dc_ht,
                     pt_huffman_table *ac_ht);

#ifdef __cplusplus
}
#endif
#endif /* DCT_H */
