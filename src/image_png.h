#ifndef IMAGE_PNG_H
#define IMAGE_PNG_H

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encode an 8-bit grayscale matrix (row-major) into PNG format.
 *
 * Uses standard PNG specification with 8-bit depth, grayscale color type (0),
 * filter type None (0), and standard zlib/DEFLATE container.
 *
 * @param data Pointer to array of width * height 8-bit pixel values.
 * @param width Image width (number of columns, e.g. 48).
 * @param height Image height (number of rows, e.g. 32).
 * @param out_png_buf Output buffer to store the encoded PNG binary.
 * @param max_buf_size Maximum capacity of out_png_buf.
 * @return Number of bytes written to out_png_buf, or 0 on error.
 */
size_t encode_gray8_png(const uint8_t *data, uint16_t width, uint16_t height,
                        uint8_t *out_png_buf, size_t max_buf_size);

/**
 * @brief Encode a 16-bit grayscale distance matrix (row-major) into PNG format.
 *
 * Uses standard PNG specification with 16-bit depth, grayscale color type (0),
 * filter type None (0), and standard zlib/DEFLATE container.
 *
 * @param distances Pointer to array of width * height 16-bit distance values.
 * @param width Image width (number of columns, e.g. 48).
 * @param height Image height (number of rows, e.g. 32).
 * @param out_png_buf Output buffer to store the encoded PNG binary.
 * @param max_buf_size Maximum capacity of out_png_buf.
 * @return Number of bytes written to out_png_buf, or 0 on error.
 */
size_t encode_gray16_png(const uint16_t *distances, uint16_t width, uint16_t height,
                         uint8_t *out_png_buf, size_t max_buf_size);

#ifdef __cplusplus
}
#endif

#endif // IMAGE_PNG_H
