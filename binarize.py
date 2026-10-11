import re

with open('src/pdf/writer.cpp', 'r') as f:
    text = f.read()

m = re.search(r'(std::vector<uint8_t> scaled_pixels;.*?if \(new_w != w \|\| new_h != h\) {)', text, re.DOTALL)
if not m:
    print('Failed to find scaled_pixels block')
    exit(1)

binarize_code = '''
            bool is_binarized = false;
            std::vector<uint8_t> binarized_pixels;
            if (is_line_art && img_channels == 1 && !raw_pixels.empty()) {
                size_t row_bytes = (w + 7) / 8;
                binarized_pixels.resize(row_bytes * h, 0);
                for (long y = 0; y < h; y++) {
                    for (long x = 0; x < w; x++) {
                        uint8_t v = raw_pixels[y * w + x];
                        if (v > 127) {
                            binarized_pixels[y * row_bytes + x / 8] |= (1 << (7 - (x % 8)));
                        }
                    }
                }
                is_binarized = true;
                new_w = w;
                new_h = h;
            }

'''

text = text.replace(m.group(1), binarize_code + m.group(1))

encoding_block_old = '''
                pt_image image = {};
                image.pixels = (!scaled_pixels.empty()) ? scaled_pixels.data() : raw_pixels.data();
                image.width = (int)new_w;
                image.height = (int)new_h;
                image.components = img_channels;
'''

encoding_block_new = '''
                pt_image image = {};
                if (is_binarized) {
                    image.pixels = binarized_pixels.data();
                    image.width = (int)new_w;
                    image.height = (int)new_h;
                    image.components = 1;
                } else {
                    image.pixels = (!scaled_pixels.empty()) ? scaled_pixels.data() : raw_pixels.data();
                    image.width = (int)new_w;
                    image.height = (int)new_h;
                    image.components = img_channels;
                }
'''

text = text.replace(encoding_block_old.strip(), encoding_block_new.strip())

encode_old = '''
                if (use_flate) {
                    compressed = pt_flate_encode(image.pixels, (size_t)image.width * image.height * image.components);
                } else {
'''

encode_new = '''
                if (use_flate || is_binarized) {
                    size_t raw_len = is_binarized ? ((size_t)image.width + 7) / 8 * image.height : (size_t)image.width * image.height * image.components;
                    compressed = pt_flate_encode(image.pixels, raw_len);
                    use_flate = true; // force flate for binarized
                } else {
'''
text = text.replace(encode_old.strip(), encode_new.strip())

text = text.replace('std::string bpc_str = "8";', 'std::string bpc_str = is_binarized ? "1" : "8";')

box_old = 'if (new_w != w || new_h != h) {'
box_new = 'if (!is_binarized && (new_w != w || new_h != h)) {'
text = text.replace(box_old, box_new)

with open('src/pdf/writer.cpp', 'w') as f:
    f.write(text)
