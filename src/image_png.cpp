#include "image_png.h"
#include <cstring>

namespace {

uint32_t calculate_adler32(const uint8_t *data, size_t len) {
    uint32_t s1 = 1;
    uint32_t s2 = 0;
    for (size_t i = 0; i < len; i++) {
        s1 = (s1 + data[i]) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    return (s2 << 16) | s1;
}

uint32_t calculate_crc32(const uint8_t *data, size_t len) {
    static uint32_t table[256];
    static bool table_init = false;
    if (!table_init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) {
                if (c & 1) {
                    c = 0xEDB88320L ^ (c >> 1);
                } else {
                    c = c >> 1;
                }
            }
            table[i] = c;
        }
        table_init = true;
    }

    uint32_t c = 0xFFFFFFFFL;
    for (size_t i = 0; i < len; i++) {
        c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFL;
}

void write_be32(uint8_t *buf, uint32_t val) {
    buf[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    buf[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    buf[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    buf[3] = static_cast<uint8_t>(val & 0xFF);
}

void write_be16(uint8_t *buf, uint16_t val) {
    buf[0] = static_cast<uint8_t>((val >> 8) & 0xFF);
    buf[1] = static_cast<uint8_t>(val & 0xFF);
}

void write_le16(uint8_t *buf, uint16_t val) {
    buf[0] = static_cast<uint8_t>(val & 0xFF);
    buf[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}

} // namespace

size_t encode_gray16_png(const uint16_t *distances, uint16_t width, uint16_t height,
                         uint8_t *out_png_buf, size_t max_buf_size) {
    if (distances == nullptr || width == 0 || height == 0 || out_png_buf == nullptr) {
        return 0;
    }

    const size_t bytes_per_row = 1 + static_cast<size_t>(width) * 2; // 1 filter byte + 2 bytes per 16-bit pixel
    const size_t raw_data_size = bytes_per_row * height;

    // Non-compressed DEFLATE supports max 65535 bytes per block
    if (raw_data_size > 65535) {
        return 0;
    }

    // Calculate required PNG size:
    // Signature (8) + IHDR chunk (25) + IDAT chunk (12 + 2 + 5 + raw_data_size + 4) + IEND chunk (12)
    const size_t idat_data_size = 2 + 5 + raw_data_size + 4;
    const size_t total_png_size = 8 + (12 + 13) + (12 + idat_data_size) + (12 + 0);

    if (total_png_size > max_buf_size) {
        return 0;
    }

    size_t pos = 0;

    // 1. PNG Signature (8 bytes)
    static const uint8_t png_signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    memcpy(&out_png_buf[pos], png_signature, 8);
    pos += 8;

    // 2. IHDR Chunk (Length=13)
    write_be32(&out_png_buf[pos], 13); // Length
    pos += 4;
    const size_t ihdr_start = pos;
    out_png_buf[pos++] = 'I';
    out_png_buf[pos++] = 'H';
    out_png_buf[pos++] = 'D';
    out_png_buf[pos++] = 'R';
    write_be32(&out_png_buf[pos], width);  // Width (4 bytes BE)
    pos += 4;
    write_be32(&out_png_buf[pos], height); // Height (4 bytes BE)
    pos += 4;
    out_png_buf[pos++] = 16; // Bit depth: 16
    out_png_buf[pos++] = 0;  // Color type: 0 (Grayscale)
    out_png_buf[pos++] = 0;  // Compression method: 0 (deflate)
    out_png_buf[pos++] = 0;  // Filter method: 0 (standard)
    out_png_buf[pos++] = 0;  // Interlace method: 0 (none)

    uint32_t ihdr_crc = calculate_crc32(&out_png_buf[ihdr_start], pos - ihdr_start);
    write_be32(&out_png_buf[pos], ihdr_crc);
    pos += 4;

    // 3. IDAT Chunk
    write_be32(&out_png_buf[pos], static_cast<uint32_t>(idat_data_size)); // Length
    pos += 4;
    const size_t idat_start = pos;
    out_png_buf[pos++] = 'I';
    out_png_buf[pos++] = 'D';
    out_png_buf[pos++] = 'A';
    out_png_buf[pos++] = 'T';

    // zlib header (2 bytes)
    out_png_buf[pos++] = 0x78; // CMF (Deflate, 32K window)
    out_png_buf[pos++] = 0x01; // FLG (FCHECK)

    // Non-compressed DEFLATE block header (5 bytes)
    out_png_buf[pos++] = 0x01; // BFINAL=1, BTYPE=00 (uncompressed)
    write_le16(&out_png_buf[pos], static_cast<uint16_t>(raw_data_size)); // LEN
    pos += 2;
    write_le16(&out_png_buf[pos], static_cast<uint16_t>(~raw_data_size)); // NLEN
    pos += 2;

    // Raw scanlines with filter type 0 (None) and big-endian 16-bit pixels
    const size_t raw_scanlines_start = pos;
    for (uint16_t y = 0; y < height; y++) {
        out_png_buf[pos++] = 0x00; // Filter byte: 0 (None)
        for (uint16_t x = 0; x < width; x++) {
            uint16_t dist = distances[static_cast<size_t>(y) * width + x];
            write_be16(&out_png_buf[pos], dist);
            pos += 2;
        }
    }

    // zlib Adler-32 checksum (4 bytes BE) over uncompressed scanlines
    uint32_t adler = calculate_adler32(&out_png_buf[raw_scanlines_start], raw_data_size);
    write_be32(&out_png_buf[pos], adler);
    pos += 4;

    uint32_t idat_crc = calculate_crc32(&out_png_buf[idat_start], pos - idat_start);
    write_be32(&out_png_buf[pos], idat_crc);
    pos += 4;

    // 4. IEND Chunk (Length=0)
    write_be32(&out_png_buf[pos], 0); // Length
    pos += 4;
    const size_t iend_start = pos;
    out_png_buf[pos++] = 'I';
    out_png_buf[pos++] = 'E';
    out_png_buf[pos++] = 'N';
    out_png_buf[pos++] = 'D';

    uint32_t iend_crc = calculate_crc32(&out_png_buf[iend_start], 4);
    write_be32(&out_png_buf[pos], iend_crc);
    pos += 4;

    return pos;
}
