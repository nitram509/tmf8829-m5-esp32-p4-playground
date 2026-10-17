#ifndef TMF8829_HELP_H
#define TMF8829_HELP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *getPreconfigName(uint8_t cfgCmd);

const char *getFpModeName(uint8_t fpMode);

void printHelp();

void printDeviceInfo();

void printState();

#ifdef __cplusplus
}
#endif

#endif // TMF8829_HELP_H
