//
// Created by Martin W. Kirst on 17.10.26.
//

#ifndef IMAGE_PGM_H
#define IMAGE_PGM_H

#include <cstdint>
#include <cstddef>

void printFramePGM(const uint8_t *frame, size_t frameLen);

void printDistanceMapPGM(const uint16_t *distances, uint16_t width, uint16_t height);

int8_t singleShot48x32HexDump();

int8_t singleShot8x8HexDump();

void printHexDump(const uint8_t *data, size_t len);

#endif //IMAGE_PGM_H
