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
#define NR_OF_MEAS_CFGS             9 /**< number of preconfiguration commands available */

// ---------------------------------------------- variables ---------------------------------------

inline tmf8829Driver tmf8829; /**< instances of tmf8829 driver */
inline int8_t stateTmf8829; /**< current state of the device */
inline int8_t currentMeasConfigNr;

extern const int measConfig[NR_OF_MEAS_CFGS];

// ---------------------------------------------- functions ---------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

struct SensorConfigParams {
    uint8_t modeCmd;          /**< 0x40..0x48 (0 to keep current mode) */
    uint16_t kiloIterations;  /**< 0 to keep mode default / current */
    uint16_t periodMs;        /**< period in milliseconds (0 = free-running) */
    uint8_t dualMode;         /**< 0 = disabled, 1 = normal dual, 2 = 8x8 LR dual */
    uint8_t nrPeaks;          /**< 1..4 (default 1) */
};

void initial_setup(uint8_t logLevelIdx, uint32_t baudrate, uint32_t spiClockSpeedInHz);

int8_t main_loop(void);

void final_clean_shutdown(void);

void preconfigure(int8_t cfgNr);

bool apply_and_reset_sensor_config(const SensorConfigParams &params);

bool apply_and_reset_sensor_mode(uint8_t cfgCmd);

void measure(void);

void stop(void);

void powerDown(void);

void reset(void);

void wakeup(void);

void enable(uint32_t imageStartAddress, const unsigned char *image, int32_t imageSizeInBytes);

#ifdef __cplusplus
}
#endif

#endif // APP_H
