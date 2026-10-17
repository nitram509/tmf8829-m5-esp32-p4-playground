#ifndef APP_H
#define APP_H

// ---------------------------------------------- includes ----------------------------------------
#include <stdint.h>

#include "tmf8829/tmf8829.h"

// ---------------------------------------------- defines -----------------------------------------
/* tmf application states */
#define TMF8829_STATE_DISABLED      0 /**< application status, device is disabled */
#define TMF8829_STATE_STANDBY       1 /**< application status, device in standby */
#define TMF8829_STATE_STOPPED       2 /**< application status, device in active mode, but no measurement ongoing */
#define TMF8829_STATE_MEASURE       3 /**< application status, device is doing measurements */
#define TMF8829_STATE_ERROR         4 /**< application status in error mode, device in unexpected behaviour */

#define NR_LOG_LEVELS               9 /**< number of log-levels in application */

// ---------------------------------------------- variables ---------------------------------------

inline tmf8829Driver tmf8829; /**< instances of tmf8829 driver */
inline int8_t stateTmf8829; /**< current state of the device */
inline int8_t currentMeasConfigNr;

// ---------------------------------------------- functions ---------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

void initial_setup(uint8_t logLevelIdx, uint32_t baudrate, uint32_t spiClockSpeedInHz);

int8_t main_loop(void);

void final_clean_shutdown(void);

void preconfigure(int8_t cfgNr);

#ifdef __cplusplus
}
#endif

#endif // APP_H
