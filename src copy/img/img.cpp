#include "img/img.h"
#include "miniz/miniz.h"
#include <turbojpeg.h>

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

pt_compressed pt_dct_encode(const pt_image *image, int quality) {
    pt_compressed result = empty_compressed();
    if (!image || !image->pixels || image->width <= 0 || image->height <= 0) return result;
    if (image->components != 1 && image->components != 3 && image->components != 4) return result;

    tjhandle compressor = tjInitCompress();
    if (!compressor) return result;

    unsigned char *jpeg_data = nullptr;
    unsigned long jpeg_size = 0;
    int pixel_format = image->components == 1 ? TJPF_GRAY :
                       image->components == 4 ? TJPF_CMYK : TJPF_RGB;
    int subsampling = image->components == 1 ? TJSAMP_GRAY :
                      TJSAMP_420;
    int clamped_quality = quality < 1 ? 1 : quality > 100 ? 100 : quality;

    int status = tjCompress2(compressor, image->pixels, image->width, 0,
                             image->height, pixel_format, &jpeg_data,
                             &jpeg_size, subsampling, clamped_quality,
                             TJFLAG_FASTDCT);
    if (status == 0 && jpeg_data && jpeg_size > 0) {
        result.data = (uint8_t *)std::malloc((size_t)jpeg_size);
        if (result.data) {
            std::memcpy(result.data, jpeg_data, (size_t)jpeg_size);
            result.size = (size_t)jpeg_size;
            result.filter = PT_FILTER_DCT;
        }
    }

    if (jpeg_data) tjFree(jpeg_data);
    tjDestroy(compressor);
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
