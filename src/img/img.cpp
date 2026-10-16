#include "img/img.h"
#include "miniz/miniz.h"
#include "../third-party/jpge/jpge.h"
#include <vector>

#include <cstdlib>
#include <cstring>

static pt_compressed empty_compressed(void) {
    pt_compressed result = {};
    result.filter = PT_FILTER_NONE;
    return result;
}

pt_compressed pt_flate_encode(const uint8_t *data, size_t len) {
    pt_compressed result = empty_compressed();
    if (!data || len == 0) return result;

    mz_ulong capacity = mz_compressBound((mz_ulong)len);
    uint8_t *encoded = (uint8_t *)std::malloc((size_t)capacity);
    if (!encoded) return result;

    mz_ulong encoded_len = capacity;
    int status = mz_compress2(encoded, &encoded_len, data, (mz_ulong)len, MZ_BEST_COMPRESSION);
    if (status != MZ_OK) {
        std::free(encoded);
        return result;
    }

    result.data = encoded;
    result.size = (size_t)encoded_len;
    result.filter = PT_FILTER_FLATE;
    return result;
}

class JpgeVectorStream : public jpge::output_stream {
public:
    std::vector<uint8_t> buf;
    virtual bool put_buf(const void* pBuf, int len) override {
        const uint8_t* p = (const uint8_t*)pBuf;
        buf.insert(buf.end(), p, p + len);
        return true;
    }
};

#include "turbojpeg.h"

uint8_t *pt_cmyk_jpeg_decode(const uint8_t *data, size_t len, int *w, int *h) {
    if (!data || len == 0 || !w || !h) return NULL;
    tjhandle tj_decompress = tj3Init(TJINIT_DECOMPRESS);
    if (!tj_decompress) return NULL;

    if (tj3DecompressHeader(tj_decompress, data, len) != 0) {
        tj3Destroy(tj_decompress);
        return NULL;
    }

    *w = tj3Get(tj_decompress, TJPARAM_JPEGWIDTH);
    *h = tj3Get(tj_decompress, TJPARAM_JPEGHEIGHT);
    int cs = tj3Get(tj_decompress, TJPARAM_COLORSPACE);

    if (cs != TJCS_YCCK && cs != TJCS_CMYK) {
        tj3Destroy(tj_decompress);
        return NULL;
    }

    uint8_t *cmyk_buf = (uint8_t*)std::malloc((size_t)(*w) * (*h) * 4);
    if (!cmyk_buf) {
        tj3Destroy(tj_decompress);
        return NULL;
    }

    if (tj3Decompress8(tj_decompress, data, len, cmyk_buf, 0, TJPF_CMYK) != 0) {
        std::free(cmyk_buf);
        tj3Destroy(tj_decompress);
        return NULL;
    }

    tj3Destroy(tj_decompress);
    return cmyk_buf;
}

pt_compressed pt_cmyk_jpeg_encode(const uint8_t *cmyk_pixels, int w, int h, int quality) {
    pt_compressed res = empty_compressed();
    if (!cmyk_pixels || w <= 0 || h <= 0) return res;

    tjhandle tj_compress = tj3Init(TJINIT_COMPRESS);
    if (!tj_compress) return res;

    int q = quality < 1 ? 1 : quality > 100 ? 100 : quality;
    tj3Set(tj_compress, TJPARAM_QUALITY, q);
    tj3Set(tj_compress, TJPARAM_OPTIMIZE, 1);
    tj3Set(tj_compress, TJPARAM_SUBSAMP, TJSAMP_444);
    tj3Set(tj_compress, TJPARAM_COLORSPACE, TJCS_YCCK);

    uint8_t *out_jpeg = NULL;
    size_t out_size = 0;
    if (tj3Compress8(tj_compress, cmyk_pixels, w, 0, h, TJPF_CMYK, &out_jpeg, &out_size) == 0 && out_jpeg && out_size > 0) {
        res.data = (uint8_t*)std::malloc(out_size);
        if (res.data) {
            std::memcpy(res.data, out_jpeg, out_size);
            res.size = out_size;
            res.filter = PT_FILTER_DCT;
        }
        tj3Free(out_jpeg);
    }

    tj3Destroy(tj_compress);
    return res;
}

pt_compressed pt_dct_encode(const pt_image *image, int quality) {
    pt_compressed result = empty_compressed();
    if (!image || !image->pixels || image->width <= 0 || image->height <= 0) return result;
    if (image->components != 1 && image->components != 3 && image->components != 4) return result;

    if (image->components == 4) {
        return pt_cmyk_jpeg_encode(image->pixels, image->width, image->height, quality);
    }

    int comp = image->components;
    int clamped_quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;

    // Use TurboJPEG for high-performance, optimal Huffman tables, and maximum sharpness
    tjhandle tj_compress = tj3Init(TJINIT_COMPRESS);
    if (tj_compress) {
        tj3Set(tj_compress, TJPARAM_QUALITY, clamped_quality);
        tj3Set(tj_compress, TJPARAM_OPTIMIZE, 1);
        int pixel_format = (comp == 1) ? TJPF_GRAY : TJPF_RGB;
        int subsamp = (comp == 1) ? TJSAMP_GRAY : TJSAMP_420;
        tj3Set(tj_compress, TJPARAM_SUBSAMP, subsamp);

        uint8_t *out_jpeg = NULL;
        size_t out_size = 0;
        if (tj3Compress8(tj_compress, image->pixels, image->width, 0, image->height, pixel_format, &out_jpeg, &out_size) == 0 && out_jpeg && out_size > 0) {
            result.data = (uint8_t*)std::malloc(out_size);
            if (result.data) {
                std::memcpy(result.data, out_jpeg, out_size);
                result.size = out_size;
                result.filter = PT_FILTER_DCT;
            }
            tj3Free(out_jpeg);
            tj3Destroy(tj_compress);
            return result;
        }
        tj3Destroy(tj_compress);
    }

    // Fallback to jpge if TurboJPEG is unavailable
    uint8_t* pixels = image->pixels;
    jpge::params params;
    params.m_quality = clamped_quality;
    params.m_two_pass_flag = true;
    if (comp == 1) {
        params.m_subsampling = jpge::Y_ONLY;
    } else {
        params.m_subsampling = jpge::H2V2;
    }

    JpgeVectorStream stream;
    jpge::jpeg_encoder encoder;
    if (encoder.init(&stream, image->width, image->height, comp, params)) {
        bool ok = true;
        for (int pass = 0; pass < (int)encoder.get_total_passes() && ok; pass++) {
            for (int y = 0; y < image->height && ok; y++) {
                const uint8_t* scanline = pixels + (size_t)y * image->width * comp;
                if (!encoder.process_scanline(scanline)) ok = false;
            }
            if (ok && !encoder.process_scanline(NULL)) ok = false;
        }
        encoder.deinit();

        if (ok && !stream.buf.empty()) {
            result.data = (uint8_t *)std::malloc(stream.buf.size());
            if (result.data) {
                std::memcpy(result.data, stream.buf.data(), stream.buf.size());
                result.size = stream.buf.size();
                result.filter = PT_FILTER_DCT;
            }
        }
    }

    return result;
}

pt_compressed pt_img_compress(const pt_image *image, int quality, pt_filter force_filter) {
    pt_compressed result = empty_compressed();
    if (!image || !image->pixels || image->width <= 0 || image->height <= 0) return result;

    if (force_filter == PT_FILTER_FLATE) {
        size_t raw_size = (size_t)image->width * (size_t)image->height * (size_t)image->components;
        return pt_flate_encode(image->pixels, raw_size);
    }
    if (force_filter == PT_FILTER_DCT) return pt_dct_encode(image, quality);
    if (force_filter != PT_FILTER_NONE) return result;

    return pt_dct_encode(image, quality);
}

pt_image pt_flate_decode(const uint8_t *data, size_t len,
                         int width, int height, int components,
                         int bits_per_component, int predictor) {
    pt_image image = {};
    if (!data || len == 0 || width <= 0 || height <= 0) { fprintf(stderr, "FLATE REJECT: dims\n"); return image; }
    if (components <= 0 || components > 4) { fprintf(stderr, "FLATE REJECT: comp %d\n", components); return image; }
    if (bits_per_component != 1 && bits_per_component != 8) { fprintf(stderr, "FLATE REJECT: bpc %d\n", bits_per_component); return image; }
    if (predictor != 1 && predictor != 2 && (predictor < 10 || predictor > 15)) { fprintf(stderr, "FLATE REJECT: pred %d\n", predictor); return image; }

    size_t decoded_size = 0;
    void *decoded_memory = tinfl_decompress_mem_to_heap(
        data, len, &decoded_size, TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (!decoded_memory) return image;

    const uint8_t *decoded = (const uint8_t *)decoded_memory;
    size_t row_bytes = bits_per_component == 1
        ? ((size_t)width + 7) / 8
        : (size_t)width * (size_t)components;
    size_t filtered_row_bytes = row_bytes + (predictor >= 10 ? 1 : 0);
    if (filtered_row_bytes == 0 || decoded_size < filtered_row_bytes * (size_t)height) {
        fprintf(stderr, "FLATE REJECT: dec_size %zu < expected %zu (row_bytes=%zu, h=%d)\n", decoded_size, filtered_row_bytes * (size_t)height, filtered_row_bytes, height);
        mz_free(decoded_memory);
        return image;
    }

    size_t pixel_size = (size_t)width * (size_t)height *
                        (bits_per_component == 1 ? 1 : (size_t)components);
    uint8_t *pixels = (uint8_t *)std::malloc(pixel_size);
    if (!pixels) {
        mz_free(decoded_memory);
        return image;
    }

    std::memset(pixels, 0, pixel_size);
    for (int y = 0; y < height; ++y) {
        const uint8_t *source = decoded + (size_t)y * filtered_row_bytes;
        const uint8_t *current = source + (predictor >= 10 ? 1 : 0);
        uint8_t *destination = pixels + (size_t)y * (size_t)width *
                               (bits_per_component == 1 ? 1 : (size_t)components);
        const uint8_t *previous = y > 0
            ? pixels + (size_t)(y - 1) * (size_t)width *
              (bits_per_component == 1 ? 1 : (size_t)components)
            : nullptr;

        if (predictor < 10) {
            if (predictor == 2) {
                size_t bpp = (bits_per_component == 1 ? 1 : (size_t)components);
                for (size_t x = 0; x < row_bytes; ++x) {
                    uint8_t left = x >= bpp ? destination[x - bpp] : 0;
                    destination[x] = (uint8_t)(current[x] + left);
                }
            } else {
                std::memcpy(destination, current, row_bytes);
            }
            continue;
        }

        int filter = source[0];
        for (size_t x = 0; x < row_bytes; ++x) {
            uint8_t left = x >= (bits_per_component == 1 ? 1 : (size_t)components)
                ? destination[x - (bits_per_component == 1 ? 1 : (size_t)components)] : 0;
            uint8_t up = previous ? previous[x] : 0;
            uint8_t upper_left = previous && x >= (bits_per_component == 1 ? 1 : (size_t)components)
                ? previous[x - (bits_per_component == 1 ? 1 : (size_t)components)] : 0;
            uint8_t value = current[x];
            if (filter == 1) value = (uint8_t)(value + left);
            else if (filter == 2) value = (uint8_t)(value + up);
            else if (filter == 3) value = (uint8_t)(value + ((int)left + up) / 2);
            else if (filter == 4) {
                int estimate = (int)left + (int)up - (int)upper_left;
                int left_distance = abs(estimate - (int)left);
                int up_distance = abs(estimate - (int)up);
                int upper_left_distance = abs(estimate - (int)upper_left);
                uint8_t reference = left_distance <= up_distance && left_distance <= upper_left_distance
                    ? left : up_distance <= upper_left_distance ? up : upper_left;
                value = (uint8_t)(value + reference);
            } else if (filter != 0) {
                std::free(pixels);
                mz_free(decoded_memory);
                return image;
            }
            destination[x] = value;
        }
    }

    if (bits_per_component == 1) {
        uint8_t *expanded_pixels = (uint8_t *)std::malloc((size_t)width * (size_t)height);
        if (!expanded_pixels) {
            std::free(pixels);
            mz_free(decoded_memory);
            return image;
        }
        for (int y = 0; y < height; ++y) {
            const uint8_t *row = pixels + (size_t)y * row_bytes;
            uint8_t *expanded = expanded_pixels + (size_t)y * (size_t)width;
            for (int x = 0; x < width; ++x) {
                expanded[x] = (row[x / 8] >> (7 - (x % 8)) & 1) ? 255 : 0;
            }
        }
        std::free(pixels);
        pixels = expanded_pixels;
        image.components = 1;
    } else {
        image.components = components;
    }
    image.pixels = pixels;
    image.width = width;
    image.height = height;
    image.cs = components == 1 ? PT_CS_GRAY : components == 4 ? PT_CS_CMYK : PT_CS_RGB;
    mz_free(decoded_memory);
    return image;
}

void pt_img_free(pt_image *image) {
    if (!image) return;
    std::free(image->pixels);
    image->pixels = nullptr;
    image->width = 0;
    image->height = 0;
    image->components = 0;
}

void pt_compressed_free(pt_compressed *compressed) {
    if (!compressed) return;
    std::free(compressed->data);
    compressed->data = nullptr;
    compressed->size = 0;
    compressed->filter = PT_FILTER_NONE;
}
