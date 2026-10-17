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
#include "tmf8829_shim.h"
// ---------------------------------------------- functions ---------------------------------------

/** @brief Arduino setup function is only called once at startup. Do all the HW initialisation stuff here.
 * @param logLevelIdx ...  the log level index to be used (0..8 -> see logLevels array in tmf8829_app.cpp)
 * @param  baudrate ... for the serial input the baudrate
 * @param  i2cClockSpeedInHz ... the i2c frequency
 */
#ifdef __cplusplus
extern "C" {
#endif
void initial_setup( uint8_t logLevelIdx, uint32_t baudrate, uint32_t i2cClockSpeedInHz );
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

/** @brief Arduino main loop function, is executed cyclic.
 * @return 1 if wants to be called again
 * @return 0 if program should terminate
 */
int8_t main_loop( );

/** @brief Arduino terminate function is only called once when exit key 'q' is pressed. Write a message and wait for shutdown of arduino.
 */
void final_clean_shutdown( );

#endif // TMF8829_APP_H
