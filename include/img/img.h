/*
    img.h — Pation Image Compression Subsystem
    Supports:
      - JPEG (DCT-based) from scratch — for color/gray images
      - Flate/Deflate (zlib) passthrough via miniz — for lossless
      - Run-Length Encoding — for bi-level / simple images
    SMALLAB28 SOFTWARE — ALL RIGHTS RESERVED
*/
#ifndef IMG_H
#define IMG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

/* ---------- colour-space tags ---------- */
typedef enum {
    PT_CS_GRAY    = 0,
    PT_CS_RGB     = 1,
    PT_CS_CMYK    = 2,
    PT_CS_INDEXED = 3,
} pt_colorspace;

/* ---------- filter/codec tags ---------- */
typedef enum {
    PT_FILTER_NONE     = 0,   /* raw pixels                          */
    PT_FILTER_FLATE    = 1,   /* zlib deflate (lossless)             */
    PT_FILTER_DCT      = 2,   /* DCT (JPEG-like, lossy)              */
    PT_FILTER_RLE      = 3,   /* PackBits / run-length               */
    PT_FILTER_CCITT    = 4,   /* CCITT G4 (bi-level)                 */
} pt_filter;

/* ---------- raw image descriptor ---------- */
typedef struct {
    uint8_t  *pixels;      /* row-major pixel data (no padding)   */
    int       width;
    int       height;
    int       components;  /* bytes per pixel (1=gray, 3=rgb, 4=cmyk) */
    pt_colorspace cs;
} pt_image;

/* ---------- compressed output ---------- */
typedef struct {
    uint8_t  *data;        /* malloc'd — caller must free()       */
    size_t    size;
    pt_filter filter;      /* which filter was actually used      */
} pt_compressed;

/* =====================================================================
   Public API
   ===================================================================== */

/*
 * pt_img_compress
 *   Compress a raw raster image. 'quality' is 1-100 (JPEG quality)
 *   ignored for lossless codecs.  'force_filter' == PT_FILTER_NONE means
 *   "choose automatically".
 *   Returns a pt_compressed whose .data the caller must free().
 */
pt_compressed pt_img_compress(const pt_image *img,
                              int             quality,
                              pt_filter       force_filter);

/*
 * pt_img_free — release pixel buffer owned by pt_image (if heap-allocated)
 */
void pt_img_free(pt_image *img);

/*
 * pt_compressed_free — release the malloc'd output buffer
 */
void pt_compressed_free(pt_compressed *c);

/* low-level: individual codecs (also used internally) */
pt_compressed pt_rle_encode   (const uint8_t *data, size_t len);
pt_compressed pt_flate_encode (const uint8_t *data, size_t len);
pt_compressed pt_dct_encode   (const pt_image *img, int quality);

pt_image pt_flate_decode(const uint8_t *data, size_t len,
                         int width, int height, int components,
                         int bits_per_component, int predictor);

/* Decode a CMYK/YCCK JPEG into packed CMYK pixels */
uint8_t *pt_cmyk_jpeg_decode(const uint8_t *data, size_t len, int *w, int *h);

/* Encode packed CMYK pixels into an Adobe YCCK JPEG */
pt_compressed pt_cmyk_jpeg_encode(const uint8_t *cmyk_pixels, int w, int h, int quality);

#ifdef __cplusplus
}
#endif
#endif /* IMG_H */
