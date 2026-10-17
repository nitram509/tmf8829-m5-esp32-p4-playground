#ifndef HELP_H
#define HELP_H

#include <cstdint>

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

#endif // HELP_H
