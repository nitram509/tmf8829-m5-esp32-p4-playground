/**************************************************************************************************
* Copyright © 2024 ams-OSRAM AG                                                                   *
* All rights are reserved.                                                                        *
*                                                                                                 *
* FOR FULL LICENSE TEXT SEE LICENSES-MIT.TXT                                                      *
*                                                                                                 *
**************************************************************************************************/

/** @file This is the tmf8829 arduino uno driver example console application. 
 */

#ifndef TMF8829_APP_H
#define TMF8829_APP_H

// ---------------------------------------------- includes ----------------------------------------
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------- defines -----------------------------------------
/* tmf application states */
#define TMF8829_STATE_DISABLED      0 /**< application status, device is disabled */
#define TMF8829_STATE_STANDBY       1 /**< application status, device in standby */
#define TMF8829_STATE_STOPPED       2 /**< application status, device in active mode, but no measurement ongoing */
#define TMF8829_STATE_MEASURE       3 /**< application status, device is doing measurements */
#define TMF8829_STATE_ERROR         4 /**< application status in error mode, device in unexpected behaviour */

#define NR_LOG_LEVELS               9 /**< number of log-levels in application */

// ---------------------------------------------- functions ---------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

void initial_setup( uint8_t logLevelIdx, uint32_t baudrate, uint32_t spiClockSpeedInHz );
int8_t main_loop( void );
void final_clean_shutdown( void );

int8_t singleShot48x32HexDump( void );
int8_t singleShot8x8HexDump( void );
void printHexDump( const uint8_t * data, size_t len );
void printDistanceMapPGM( const uint16_t * distances, uint16_t width, uint16_t height );
void printFramePGM( const uint8_t * frame, size_t frameLen );

#ifdef __cplusplus
}
#endif

#endif // TMF8829_APP_H
