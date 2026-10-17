#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tmf8829/tmf8829.h"

#include "image_pgm.h"
#include "app.h"
#include "net.h"


/** @brief Function to decode a result frame payload and print it in ASCII PGM (P2) format.
 *  @param frame ... pointer to raw frame data buffer (including pre-header and frame header)
 *  @param frameLen ... total length of raw frame data
 */
void printFramePGM(const uint8_t *frame, size_t frameLen) {
    if (frame == nullptr || frameLen < (TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE)) {
        return;
    }

    uint8_t fid = frame[TMF8829_PRE_HEADER_SIZE] & TMF8829_FID_MASK;
    if (fid != TMF8829_FID_RESULTS) {
        PRINT_CONST_STR(F( "#Err,Frame is not a results frame" ));
        PRINT_LN();
        return;
    }

    uint8_t layout = frame[TMF8829_PRE_HEADER_SIZE + 1];
    uint8_t pixelSize = tmf8829GetPixelSize(layout);
    if (pixelSize == 0) {
        pixelSize = 3;
    }

    const size_t pixelStart = TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE;
    const size_t pixelDataBytes = (frameLen > (pixelStart + TMF8829_FRAME_FOOTER_SIZE))
                                      ? (frameLen - pixelStart - TMF8829_FRAME_FOOTER_SIZE)
                                      : 0;
    uint16_t totalPixels = pixelDataBytes / pixelSize;
    if (totalPixels == 0) {
        return;
    }

    uint16_t width = 8, height = 8;
    if (totalPixels == 64) {
        width = 8;
        height = 8;
    } else if (totalPixels == 256) {
        width = 16;
        height = 16;
    } else if (totalPixels == 768) {
        width = 48;
        height = 16;
    } else if (totalPixels == 1536) {
        width = 48;
        height = 32;
    }

    static uint16_t distances[1536] = {};

    for (uint16_t i = 0; i < totalPixels && i < 1536; i++) {
        size_t offset = pixelStart + (i * pixelSize);
        size_t idx = 0;
        if (layout & TMF8829_CFG_RESULT_FORMAT_NOISE_STRENGTH_MASK) { idx += 2; }
        if (layout & TMF8829_CFG_RESULT_FORMAT_XTALK_MASK) { idx += 2; }

        if ((offset + idx + 2) <= frameLen) {
            uint16_t dist = (uint16_t) frame[offset + idx] | ((uint16_t) frame[offset + idx + 1] << 8);
            if (tmf8829.clkCorrectionEnable) {
                dist = tmf8829CorrectDistance(&tmf8829, dist);
            }
            distances[i] = dist;
        }
    }

    printDistanceMapPGM(distances, width, height);
}

/** @brief Function to print distance map in ASCII PGM (P2) format.
 *  @param distances ... pointer to array of uint16 distance values (in row-major order)
 *  @param width ... number of columns
 *  @param height ... number of rows
 */
void printDistanceMapPGM(const uint16_t *distances, uint16_t width, uint16_t height) {
    if (distances == nullptr || width == 0 || height == 0) {
        return;
    }

    uint32_t totalPixels = (uint32_t) width * height;
    uint16_t maxDist = 0;
    for (uint32_t i = 0; i < totalPixels; i++) {
        if (distances[i] > maxDist) {
            maxDist = distances[i];
        }
    }

    uint16_t maxVal = (maxDist > 255) ? maxDist : 255;

    PRINT_CONST_STR(F( "P2" ));
    PRINT_LN();
    PRINT_CONST_STR(F( "# TMF8829 " ));
    PRINT_INT(width);
    PRINT_CHAR('x');
    PRINT_INT(height);
    PRINT_CONST_STR(F( " Distance Map" ));
    PRINT_LN();
    PRINT_INT(width);
    PRINT_CHAR(' ');
    PRINT_INT(height);
    PRINT_LN();
    PRINT_UINT(maxVal);
    PRINT_LN();

    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {
            PRINT_UINT(distances[y * width + x]);
            if (x < (width - 1)) {
                PRINT_CHAR(' ');
            }
        }
        PRINT_LN();
    }
}

/** @brief Sets mode 48x32, enables measurement, reads a single full 48x32 frame (2 sub-frames),
 *  stops measurement, and prints the raw frames as hex dumps and decoded 48x32 distance matrix in PGM format.
 *  @return APP_SUCCESS_OK on success, or error code on failure.
 */
int8_t singleShot48x32HexDump() {
    if (stateTmf8829 == TMF8829_STATE_DISABLED || stateTmf8829 == TMF8829_STATE_ERROR) {
        PRINT_CONST_STR(F( "Cannot perform single shot: Device is not initialized." ));
        PRINT_LN();
        PRINT_CONST_STR(F( "Please press 'e' to enable device and download firmware first." ));
        PRINT_LN();
        return APP_ERROR_NO_RESULT;
    }

    if (stateTmf8829 == TMF8829_STATE_MEASURE) {
        tmf8829DisableInterrupts(&tmf8829, 0xFF);
        tmf8829StopMeasurement(&tmf8829);
        stateTmf8829 = TMF8829_STATE_STOPPED;
    }

    PRINT_CONST_STR(F( "==================================================" ));
    PRINT_LN();
    PRINT_CONST_STR(F( "=== TMF8829 48x32 Single-Shot Capture & Hexdump ===" ));
    PRINT_LN();
    PRINT_CONST_STR(F( "==================================================" ));
    PRINT_LN();

    // 1. Set mode 48x32 Default
    PRINT_CONST_STR(F( "[1/4] Configuring 48x32 Default mode..." ));
    PRINT_LN();
    currentMeasConfigNr = 7; // 48x32 Default mode (measConfig[7])
    preconfigure(TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_48X32);

    // 2. Enable measurement
    PRINT_CONST_STR(F( "[2/4] Starting measurement (capturing 48x32 sub-frames)..." ));
    PRINT_LN();
    tmf8829ClrAndEnableInterrupts(&tmf8829, TMF8829_APP_INT_RESULTS | TMF8829_APP_INT_HISTOGRAMS);
    int8_t startStat = tmf8829StartMeasurement(&tmf8829);
    if (startStat != APP_SUCCESS_OK) {
        PRINT_CONST_STR(F( "#Err,Failed to start measurement" ));
        PRINT_LN();
        tmf8829DisableInterrupts(&tmf8829, 0xFF);
        stateTmf8829 = TMF8829_STATE_STOPPED;
        return startStat;
    }
    stateTmf8829 = TMF8829_STATE_MEASURE;

    // 3. Wait for 48x32 frames (2 sub-frames: even and odd rows)
    static uint16_t distances[1536];
    memset(distances, 0, sizeof(distances));
    static uint8_t rawFrames[2][4096];
    memset(rawFrames, 0, sizeof(rawFrames));
    size_t rawFrameSizes[2] = {0, 0};
    static uint8_t frameBuffer[4096];
    memset(frameBuffer, 0, sizeof(frameBuffer));
    bool subFrameSeen[2] = {false, false};
    int framesCaptured = 0;
    const int timeoutMs = 15000;
    int elapsedMs = 0;
    int lastDotMs = 0;

    while ((!subFrameSeen[0] || !subFrameSeen[1]) && elapsedMs < timeoutMs && framesCaptured < 4) {
        uint8_t intStatus = tmf8829GetAndClrInterrupts(&tmf8829, TMF8829_APP_INT_RESULTS | TMF8829_APP_INT_HISTOGRAMS);
        uint8_t fifoStatusReg = 0xFF;
        rxReg(&tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFOSTATUS, 1, &fifoStatusReg);
        bool fifoHasData = ((fifoStatusReg & 0x04) == 0); // Bit 2 (txfifo_empty): 0 = data available in FIFO

        if ((intStatus & TMF8829_APP_INT_RESULTS) || fifoHasData) {
            // Drain frames from FIFO while data is available
            while ((!subFrameSeen[0] || !subFrameSeen[1]) && framesCaptured < 4) {
                size_t totalFrameSize = 0;
                int8_t rxStat = rxReg(&tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFOSTATUS,
                                      TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE,
                                      frameBuffer);
                if (rxStat != APP_SUCCESS_OK) {
                    break;
                }

                uint8_t fid = frameBuffer[TMF8829_PRE_HEADER_SIZE] & TMF8829_FID_MASK;
                if (fid != TMF8829_FID_RESULTS && fid != TMF8829_FID_HISTOGRAMS) {
                    break;
                }

                totalFrameSize = TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE;
                uint8_t layout = frameBuffer[TMF8829_PRE_HEADER_SIZE + 1];
                uint16_t payload = tmf8829GetUint16(frameBuffer + TMF8829_PRE_HEADER_SIZE + 2);

                uint16_t sizeToRead = 0;
                if (payload >= (TMF8829_FRAME_HEADER_SIZE - TMF8829_FRAME_HEADER_OFFSET)) {
                    sizeToRead = payload - (TMF8829_FRAME_HEADER_SIZE - TMF8829_FRAME_HEADER_OFFSET);
                }

                if (sizeToRead > 0 && (totalFrameSize + sizeToRead <= sizeof(frameBuffer))) {
                    uint16_t remaining = sizeToRead;
                    while (remaining > 0) {
                        uint16_t chunk = (remaining > DATA_BUFFER_SIZE) ? DATA_BUFFER_SIZE : remaining;
                        rxStat = rxReg(&tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFO, chunk,
                                       frameBuffer + totalFrameSize);
                        if (rxStat != APP_SUCCESS_OK) {
                            PRINT_CONST_STR(F( "#Err,Failed to read FIFO payload." ));
                            PRINT_LN();
                            break;
                        }
                        totalFrameSize += chunk;
                        remaining -= chunk;
                    }
                }

                if (fid == TMF8829_FID_RESULTS) {
                    uint8_t subResult = 0;
                    if (layout & TMF8829_RESULT_FRAME_SUBIDX_MASK) {
                        subResult = 1;
                    } else if (subFrameSeen[0] && !subFrameSeen[1]) {
                        subResult = 1;
                    } else if (!subFrameSeen[0] && subFrameSeen[1]) {
                        subResult = 0;
                    }

                    PRINT_CONST_STR(F( " -> Captured Sub-frame " ));
                    PRINT_INT(subResult);
                    PRINT_CONST_STR(F( ( subResult == 0 ) ? " (Even Rows)" : " (Odd Rows)" ));
                    PRINT_CONST_STR(F( " (Layout=0x" ));
                    PRINT_UINT_HEX(layout);
                    PRINT_CONST_STR(F( ") at " ));
                    PRINT_INT(elapsedMs);
                    PRINT_CONST_STR(F( " ms" ));
                    PRINT_LN();
                    uint8_t pixelSize = tmf8829GetPixelSize(layout);
                    if (pixelSize == 0) {
                        pixelSize = 3;
                    }

                    const size_t pixelStart = TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE;
                    const int pixelsInSubFrame = 768; // 16 rows * 48 columns

                    for (int m = 0; m < pixelsInSubFrame; m++) {
                        int r = m / 48; // row in sub-frame (0..15)
                        int c = m % 48; // column (0..47)
                        int y = (r * 2) + subResult; // full matrix row (0..31)
                        int x = c;
                        int fullIdx = y * 48 + x;

                        size_t offset = pixelStart + (m * pixelSize);
                        if ((offset + 2) <= totalFrameSize) {
                            size_t idx = 0;
                            if (layout & TMF8829_CFG_RESULT_FORMAT_NOISE_STRENGTH_MASK) { idx += 2; }
                            if (layout & TMF8829_CFG_RESULT_FORMAT_XTALK_MASK) { idx += 2; }
                            if ((offset + idx + 2) <= totalFrameSize) {
                                uint16_t dist = (uint16_t) frameBuffer[offset + idx] | (
                                                    (uint16_t) frameBuffer[offset + idx + 1] << 8);
                                if (tmf8829.clkCorrectionEnable) {
                                    dist = tmf8829CorrectDistance(&tmf8829, dist);
                                }
                                distances[fullIdx] = dist;
                            }
                        }
                    }

                    if (subResult < 2) {
                        memcpy(rawFrames[subResult], frameBuffer, totalFrameSize);
                        rawFrameSizes[subResult] = totalFrameSize;
                    }
                    subFrameSeen[subResult] = true;
                    framesCaptured++;
                }

                // Check if another frame is ready in the FIFO
                fifoStatusReg = 0xFF;
                rxReg(&tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFOSTATUS, 1, &fifoStatusReg);
                if ((fifoStatusReg & 0x04) != 0) // Bit 2 is 1 -> FIFO is empty
                {
                    break;
                }
            }
        } else {
            if (elapsedMs - lastDotMs >= 1000) {
                PRINT_CHAR('.');
                lastDotMs = elapsedMs;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
        elapsedMs += 5;
    }
    if (lastDotMs > 0) {
        PRINT_LN();
    }

    // 4. Stop measurement
    PRINT_CONST_STR(F( "[3/4] Stopping measurement..." ));
    PRINT_LN();
    tmf8829DisableInterrupts(&tmf8829, 0xFF);
    tmf8829StopMeasurement(&tmf8829);
    stateTmf8829 = TMF8829_STATE_STOPPED;

    if (framesCaptured == 0) {
        PRINT_CONST_STR(F( "#Err,Timeout waiting for frames from sensor." ));
        PRINT_LN();
        return APP_ERROR_NO_RESULT;
    }

    // 5. Print decoded info and Hexdumps
    PRINT_CONST_STR(F( "[4/4] Printing frame hexdumps and PGM image:" ));
    PRINT_LN();

    for (int i = 0; i < 2; i++) {
        if (!subFrameSeen[i]) { continue; }
        const uint8_t *buf = rawFrames[i];
        size_t sz = rawFrameSizes[i];
        uint8_t fid = buf[TMF8829_PRE_HEADER_SIZE] & TMF8829_FID_MASK;
        uint8_t layout = buf[TMF8829_PRE_HEADER_SIZE + 1];
        uint8_t subResult = (layout & TMF8829_RESULT_FRAME_SUBIDX_MASK) ? 1 : 0;
        uint16_t payload = tmf8829GetUint16(buf + TMF8829_PRE_HEADER_SIZE + 2);
        uint32_t frameNum = tmf8829GetUint32(buf + TMF8829_PRE_HEADER_SIZE + 4);
        uint32_t tTick = tmf8829GetUint32(buf + 1);
        uint16_t eofMarker = (sz >= 2) ? tmf8829GetUint16(buf + sz - TMF8829_FRAME_EOF_SIZE) : 0;

        PRINT_CONST_STR(F( "--- Frame " ));
        PRINT_INT(i + 1);
        PRINT_CONST_STR(F( " (Sub-frame " ));
        PRINT_INT(subResult);
        PRINT_CONST_STR(F( ( subResult == 0 ) ? " - Even Rows) ---" : " - Odd Rows) ---" ));
        PRINT_LN();
        PRINT_CONST_STR(F( " Frame Type (FID): 0x" ));
        PRINT_UINT_HEX(fid);
        if (fid == TMF8829_FID_RESULTS) { PRINT_CONST_STR(F( " (RESULTS)" )); }
        PRINT_LN();
        PRINT_CONST_STR(F( " Layout: 0x" ));
        PRINT_UINT_HEX(layout);
        PRINT_LN();
        PRINT_CONST_STR(F( " Payload: " ));
        PRINT_INT(payload);
        PRINT_CONST_STR(F( " bytes" ));
        PRINT_LN();
        PRINT_CONST_STR(F( " Frame Number: " ));
        PRINT_UINT(frameNum);
        PRINT_LN();
        PRINT_CONST_STR(F( " SysTick: " ));
        PRINT_UINT(tTick);
        PRINT_LN();
        PRINT_CONST_STR(F( " Total Raw Frame Bytes: " ));
        PRINT_INT(sz);
        PRINT_LN();
        PRINT_CONST_STR(F( " End-of-Frame Marker: 0x" ));
        PRINT_UINT_HEX(eofMarker);
        if (eofMarker == TMF8829_FRAME_EOF) { PRINT_CONST_STR(F( " (OK - 0xE0F7)" )); } else {
            PRINT_CONST_STR(F( " (MISMATCH)" ));
        }
        PRINT_LN();
        PRINT_LN();

        PRINT_CONST_STR(F( "--- Raw Frame Hexdump ---" ));
        PRINT_LN();
        printHexDump(buf, sz);
        PRINT_LN();

        send_raw_frame_udp(buf, sz);
    }

    PRINT_CONST_STR(F( "--- Decoded 48x32 Frame PGM Format ---" ));
    PRINT_LN();
    printDistanceMapPGM(distances, 48, 32);
    PRINT_CONST_STR(F( "==================================================" ));
    PRINT_LN();

    return APP_SUCCESS_OK;
}

/** @brief Compatibility wrapper for single-shot frame capture.
 */
int8_t singleShot8x8HexDump() {
    return singleShot48x32HexDump();
}

/** @brief Function to print buffer contents as formatted canonical hex dump.
 *  @param data ... pointer to byte array
 *  @param len ... length of byte array
 */
void printHexDump(const uint8_t *data, size_t len) {
    if (data == nullptr || len == 0) {
        return;
    }
    for (size_t i = 0; i < len; i += 16) {
        char lineBuf[128];
        int pos = snprintf(lineBuf, sizeof(lineBuf), "%08X: ", static_cast<unsigned int>(i));

        // Hex bytes
        for (size_t j = 0; j < 16; j++) {
            if (j == 8 && pos < (int) sizeof(lineBuf)) {
                lineBuf[pos++] = ' ';
            }
            if ((i + j) < len) {
                pos += snprintf(lineBuf + pos, sizeof(lineBuf) - pos, "%02X ", data[i + j]);
            } else {
                pos += snprintf(lineBuf + pos, sizeof(lineBuf) - pos, "   ");
            }
        }

        // ASCII characters
        if (pos < (int) sizeof(lineBuf)) {
            pos += snprintf(lineBuf + pos, sizeof(lineBuf) - pos, " |");
        }
        for (size_t j = 0; j < 16 && (i + j) < len && pos < (int) sizeof(lineBuf); j++) {
            uint8_t c = data[i + j];
            lineBuf[pos++] = (c >= 32 && c <= 126) ? (char) c : '.';
        }
        if (pos < (int) sizeof(lineBuf)) {
            lineBuf[pos++] = '|';
        }
        lineBuf[pos] = '\0';
        PRINT_STR(lineBuf);
        PRINT_LN();
    }
}
