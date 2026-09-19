/**************************************************************************************************
* Copyright © 2024 ams-OSRAM AG                                                                   *
* All rights are reserved.                                                                        *
*                                                                                                 *
* FOR FULL LICENSE TEXT SEE LICENSES-MIT.TXT                                                      *
*                                                                                                 *
**************************************************************************************************/

/* TMF8829 Arduino Uno sample program */

// ---------------------------------------------- includes ----------------------------------------
#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tmf8829_shim.h"
#include "tmf8829.h"
#include "tmf8829_app.h"
#include "tmf8829_firmware.h"
#include "tmf8829_help.h"

#define TMF8829_APPLICATION_MINOR_VERSION    5


#define NR_OF_MEAS_CFGS 9 /**< number of preconfiguration commands that are available, see TMF8829_CMD_STAT */

/* tmf application states */
#define TMF8829_STATE_DISABLED      0 /**< application status, device is disabled */
#define TMF8829_STATE_STANDBY       1 /**< application status, device in standby */
#define TMF8829_STATE_STOPPED       2 /**< application status, device in active mode, but no measurement ongoing */
#define TMF8829_STATE_MEASURE       3 /**< application status, device is doing measurements */
#define TMF8829_STATE_ERROR         4 /**< application status in error mode, device in unexpected behaviour */

#define NR_LOG_LEVELS               9 /**< number of log-levels in application */

#define NR_REGS_PER_LINE            8 /**< number of registers that are printed in the dump on one line */

#define TMF8829_BINARY_BUF_SIZE     ( TMF8829_CFG_PAGE_SIZE + 5 ) /**< maximum binary command payload size with 5 spare bytes */

/* binary command identifiers */
#define TMF8829_BINARY_CMD_CONFIGURE      0x31  /**< sets arbitrary configuration */
#define TMF8829_BINARY_CMD_PRE_CONFIGURE  0x32  /**< sets pre-configuration */
#define TMF8829_BINARY_CMD_CHAR_MODE      0x00  /**< not a valid command identifier, indicates that the application is in character input mode */
#define TMF8829_BINARY_CMD_PENDING        0xFF  /**< not a valid command identifier, indicates that the application is in binary input mode awaiting a command identifier */

// ---------------------------------------------- constants -----------------------------------------
/** @brief logLevels to increase/decrease logging
 */
const uint8_t logLevels[ NR_LOG_LEVELS ] = 
{ TMF8829_LOG_LEVEL_NONE
, TMF8829_LOG_LEVEL_ERROR
, TMF8829_LOG_LEVEL_RESULTS_HEADER
, TMF8829_LOG_LEVEL_RESULTS
, TMF8829_LOG_LEVEL_CLK_CORRECTION
, TMF8829_LOG_LEVEL_INFO
, TMF8829_LOG_LEVEL_VERBOSE
, TMF8829_LOG_LEVEL_I2C
, TMF8829_LOG_LEVEL_DEBUG
};

/** @brief measCfg holds the supported pre-configurations by the TMF8829 device.
 */
const int measCfg[NR_OF_MEAS_CFGS] = 
{
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_8X8, 
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_8X8_LONG_RANGE,
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_8X8_HIGH_ACCURACY,
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_16X16,
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_16X16_HIGH_ACCURACY,
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_32X32,
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_32X32_HIGH_ACCURACY,
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_48X32,
  TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_48X32_HIGH_ACCURACY
};

// ---------------------------------------------- variables -----------------------------------------

tmf8829Driver tmf8829;            /**< instances of tmf8829 driver */
uint8_t logLevel;                 /**< current log level of the application */
int8_t stateTmf8829;              /**< current state of the device */
int8_t configNr;                  /**< this sample application has only a few configurations it will loop through, the variable keeps track of that */
int8_t clkCorrectionOn;           /**< if non-zero clock correction is on */
volatile uint8_t irqTriggered;    /**< interrupt is triggered or not */
uint8_t binaryCmd;                /**< currently active binary command identifier (if any) */
uint8_t binaryBufFill;            /**< fill level of the binary command payload buffer */
uint8_t binaryBuf[TMF8829_BINARY_BUF_SIZE]; /**< binary command payload buffer */

// ---------------------------------------------- function declaration ------------------------------
static void printDeviceInfo();
static void printState();
static void printRegisters( uint8_t regAddr, uint16_t len, char seperator );
static void resetAppState();
static void printConfigurationDetails();

// ---------------------------------------------- functions  ----------------------------------------
/******************************************************************************/
/* TMF8829 Device Functions                                                   */
/******************************************************************************/


/** @brief Function prints verbose details of the currently active configuration.
 */
static void printConfigurationDetails ( )
{
  if ( stateTmf8829 == TMF8829_STATE_DISABLED || stateTmf8829 == TMF8829_STATE_ERROR )
  {
    PRINT_CONST_STR( F( "Cannot read configuration: Device is not initialized." ) );
    PRINT_LN( );
    return;
  }

  int8_t stat = tmf8829GetConfiguration( &tmf8829 );
  if ( stat != APP_SUCCESS_OK )
  {
    PRINT_CONST_STR( F( "#Err,GetConfig failed" ) );
    PRINT_LN( );
    return;
  }

  uint16_t period = (uint16_t)tmf8829.config[TMF8829_CFG_PERIOD_MS_LSB - TMF8829_CFG_PERIOD_MS_LSB]
                  | ((uint16_t)tmf8829.config[TMF8829_CFG_PERIOD_MS_MSB - TMF8829_CFG_PERIOD_MS_LSB] << 8);
  uint16_t kiloIter = (uint16_t)tmf8829.config[TMF8829_CFG_KILO_ITERATIONS_LSB - TMF8829_CFG_PERIOD_MS_LSB]
                    | ((uint16_t)tmf8829.config[TMF8829_CFG_KILO_ITERATIONS_MSB - TMF8829_CFG_PERIOD_MS_LSB] << 8);
  uint8_t fpMode = tmf8829.config[TMF8829_CFG_FP_MODE - TMF8829_CFG_PERIOD_MS_LSB];
  uint8_t dumpHist = tmf8829.config[TMF8829_CFG_DUMP_HISTOGRAMS - TMF8829_CFG_PERIOD_MS_LSB];

  PRINT_CONST_STR( F( "=== TMF8829 Current Configuration ===" ) );
  PRINT_LN( );
  if ( configNr >= 0 && configNr < NR_OF_MEAS_CFGS )
  {
    PRINT_CONST_STR( F( " Profile: [" ) );
    PRINT_INT( configNr + 1 );
    PRINT_CONST_STR( F( "/" ) );
    PRINT_INT( NR_OF_MEAS_CFGS );
    PRINT_CONST_STR( F( "] " ) );
    PRINT_CONST_STR( getPreconfigName( measCfg[configNr] ) );
    PRINT_CONST_STR( F( " (Cmd 0x" ) );
    PRINT_UINT_HEX( measCfg[configNr] );
    PRINT_CONST_STR( F( ")" ) );
    PRINT_LN( );
  }
  else
  {
    PRINT_CONST_STR( F( " Profile: Default / Custom" ) );
    PRINT_LN( );
  }
  PRINT_CONST_STR( F( " Focal Plane Mode: " ) );
  PRINT_CONST_STR( getFpModeName( fpMode ) );
  PRINT_CONST_STR( F( " (id " ) );
  PRINT_INT( fpMode );
  PRINT_CONST_STR( F( ")" ) );
  PRINT_LN( );
  PRINT_CONST_STR( F( " Measurement Period: " ) );
  PRINT_INT( period );
  PRINT_CONST_STR( F( " ms" ) );
  PRINT_LN( );
  PRINT_CONST_STR( F( " Iterations: " ) );
  PRINT_INT( kiloIter );
  PRINT_CONST_STR( F( " kiter (" ) );
  PRINT_INT( (uint32_t)kiloIter * 1024 );
  PRINT_CONST_STR( F( " pulses)" ) );
  PRINT_LN( );
  PRINT_CONST_STR( F( " Histogram Dumping: " ) );
  if ( dumpHist )
  {
    PRINT_CONST_STR( F( "Enabled" ) );
  }
  else
  {
    PRINT_CONST_STR( F( "Disabled" ) );
  }
  PRINT_LN( );
  PRINT_CONST_STR( F( "=====================================" ) );
  PRINT_LN( );
}

/** @brief  Function will do a pre-configuration.
 * @param cfgNr ... preconfiguration  eq.: TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_8X8
 */
void preconfigure ( int8_t cfgNr )
{
  int8_t stat = tmf8829Command(&tmf8829, cfgNr);

  if ( stat == APP_SUCCESS_OK )
  {
    // Write and commit the loaded configuration page to active settings
    tmf8829CmdWritePage( &tmf8829 );
    PRINT_CONST_STR( "Preconfig 0x" );
    PRINT_UINT_HEX( cfgNr );
    PRINT_CONST_STR( " (" );
    PRINT_CONST_STR( getPreconfigName( cfgNr ) );
    PRINT_CONST_STR( ")" );
    PRINT_LN( );
    printConfigurationDetails( );
    // Ensure the page is closed again after reading configuration
    tmf8829CmdWritePage( &tmf8829 );
  }
  else
  {
    PRINT_CONST_STR( "#Err" );
    PRINT_CHAR( SEPARATOR );
    PRINT_CONST_STR( "Config" );
    PRINT_LN( );
  }
}

/** @brief  Function to get the configuration from the Tmf8829 device.
 *  After that the configuration is printed.
 */
void getConfiguration ( )
{
  if ( stateTmf8829 == TMF8829_STATE_STOPPED )
  {
    tmf8829GetConfiguration( &tmf8829 );
    PRINT_CONST_STR( F(  "#Config " ) );
    for ( int i = 0; i < TMF8829_CFG_PAGE_SIZE; i++ )
    {
      PRINT_UINT_HEX(tmf8829.config[i]);
      PRINT_CHAR( SEPARATOR );
    }
    PRINT_LN( );
    printConfigurationDetails( );
  }
  else
  {
    PRINT_CONST_STR( F( "Cannot get configuration: Device must be in STOPPED state." ) );
    PRINT_LN( );
  }
}

/** @brief  Function to enable the Tmf8829 device with a firmware download and Ram application start.
  * The configuration and device specific information is read and printed.
  * This function does the initialize steps for the application too.
  * @param imageStartAddress ... tmf8829 memory start address 
  * @param image ... pointer to the image
  * @param imageSizeInBytes ... image size
 */
void enable ( uint32_t imageStartAddress, const unsigned char * image, int32_t imageSizeInBytes )
{
  int8_t status;

  if ( stateTmf8829 == TMF8829_STATE_DISABLED || stateTmf8829 == TMF8829_STATE_ERROR )
  {
    tmf8829Enable( &tmf8829 );
    delayInMicroseconds( ENABLE_TIME_MS * 1000);
    tmf8829ClkCorrection( &tmf8829, clkCorrectionOn ); 
    tmf8829SetLogLevel( &tmf8829, logLevels[ logLevel ] );
    tmf8829PowerUp( &tmf8829 );
    if ( tmf8829IsCpuReady( &tmf8829, CPU_READY_TIME_MS) )
    {
      PRINT_CONST_STR( F( " CPU ready" ) );

      tmf8829BootloaderCmdI2cOff(&tmf8829); // SPI is used

      PRINT_CONST_STR( F( " DWNL FW and start App" ) );
      status = tmf8829DownloadFirmware( &tmf8829, imageStartAddress, image, imageSizeInBytes, 1 /* over fifo */ );
      PRINT_LN( );
      
      if ( status == BL_SUCCESS_OK ) 
      {
        resetAppState();
        tmf8829GetConfiguration(&tmf8829);
        stateTmf8829 = TMF8829_STATE_STOPPED;
        tmf8829ReadDeviceInfo( &tmf8829 );
        printDeviceInfo( );
        printConfigurationDetails( );
      }
      else
      {
        stateTmf8829 = TMF8829_STATE_ERROR;
      }
    }
    else
    {
      stateTmf8829 = TMF8829_STATE_ERROR;
    }
  } // else device is already enabled
  else
  {
    tmf8829ReadDeviceInfo( &tmf8829 );
    printDeviceInfo( );
    printConfigurationDetails( );
  }
}

/** @brief  Function to set the histogram dumping configuration on the Tmf8829 device.
*/
void histogramDumping ( ) 
{
  if ( stateTmf8829 == TMF8829_STATE_STOPPED )
  {
    int8_t stat = tmf8829GetConfiguration(&tmf8829);
    if ( stat == APP_SUCCESS_OK)
    {
      if (tmf8829.config[TMF8829_CFG_DUMP_HISTOGRAMS-TMF8829_CFG_PERIOD_MS_LSB])
      {
        tmf8829.config[TMF8829_CFG_DUMP_HISTOGRAMS-TMF8829_CFG_PERIOD_MS_LSB] = 0;
      }
      else
      {
        tmf8829.config[TMF8829_CFG_DUMP_HISTOGRAMS-TMF8829_CFG_PERIOD_MS_LSB] = 1;
      }
      stat = tmf8829SetConfiguration(&tmf8829);
    }
    if ( stat == APP_SUCCESS_OK)
    {
    PRINT_CONST_STR( F(  "Dump Histogram Frames is " ) );
    PRINT_INT( tmf8829.config[TMF8829_CFG_DUMP_HISTOGRAMS-TMF8829_CFG_PERIOD_MS_LSB] );
    PRINT_LN( );
    }
    else
    {
      PRINT_CONST_STR( F(  "#Err" ) );
      PRINT_CHAR( SEPARATOR );
      PRINT_CONST_STR( F(  "Config" ) );
    }
  }
  else
  {
    PRINT_CONST_STR( F(  "could not change to histogram dumping state" ) );
    PRINT_LN( );
  }
}

/** @brief  Function will start a measurement.
 */
void measure ( )
{
  if ( stateTmf8829 == TMF8829_STATE_STOPPED )
  {
    tmf8829ClrAndEnableInterrupts( &tmf8829, TMF8829_APP_INT_RESULTS | TMF8829_APP_INT_HISTOGRAMS );
    tmf8829StartMeasurement( &tmf8829 );
    stateTmf8829 = TMF8829_STATE_MEASURE;
  }
  else
  {
    PRINT_CONST_STR( F(  "no start of measurement, wrong state" ) );
    PRINT_LN( );
  }
}

/** @brief Function will stop a measurement.
 */
void stop ( )
{
  if ( stateTmf8829 == TMF8829_STATE_MEASURE || stateTmf8829 == TMF8829_STATE_STOPPED )
  {
    tmf8829StopMeasurement( &tmf8829 );
    tmf8829DisableInterrupts( &tmf8829, 0xFF );               // just disable all
    stateTmf8829 = TMF8829_STATE_STOPPED;
  }
}

/** @brief Function will power down the device by setting POFF=1 bit.
 */
void powerDown ( )
{
  if ( stateTmf8829 == TMF8829_STATE_MEASURE )      // stop a measurement first
  {
    tmf8829StopMeasurement( &tmf8829 );
    tmf8829DisableInterrupts( &tmf8829, 0xFF );     // just disable all
    stateTmf8829 = TMF8829_STATE_STOPPED;
  }
  if ( stateTmf8829 == TMF8829_STATE_STOPPED )
  {
    tmf8829Standby( &tmf8829 );
    stateTmf8829 = TMF8829_STATE_STANDBY;
    PRINT_CONST_STR( F( "TMF8829 powered down (STANDBY mode)." ) );
    PRINT_LN( );
  }
}

/** @brief Function to perform a soft reset.
 */
void reset ( )
{
  if ( stateTmf8829 != TMF8829_STATE_DISABLED )
  {
    tmf8829Reset( &tmf8829 );
    PRINT_CONST_STR( F(  "Reset TMF8829" ) );
    PRINT_LN( );
    stateTmf8829 = TMF8829_STATE_STOPPED;
  }
}

/** @brief Function to perform a wakeup.
 */
void wakeup ( )
{
  if ( stateTmf8829 == TMF8829_STATE_STANDBY )
  {
    tmf8829Wakeup( &tmf8829 );
    if ( tmf8829IsCpuReady( &tmf8829, CPU_READY_TIME_MS ) )
    {
      stateTmf8829 = TMF8829_STATE_STOPPED;
    }
    else
    {
      stateTmf8829 = TMF8829_STATE_ERROR;
    }
  }
}

/** @brief  Function to read registers and print the content.
  * @param regAddr ... first register address
  * @param len ... len of registers to be read and printed
  * @param seperator ... seperator must be either " " or "," 
 */
void printRegisters ( uint8_t regAddr, uint16_t len, char seperator )
{
  if ( stateTmf8829 != TMF8829_STATE_DISABLED )
  {
    uint8_t buf[NR_REGS_PER_LINE];
    uint16_t i;
    uint8_t j;

    for ( i = 0; i < len; i += NR_REGS_PER_LINE ) // if len is not a multiple of 8, we will print a bit more registers ....
    {
      uint8_t * ptr = buf;    
      spiRxReg( &tmf8829, tmf8829.i2cSlaveAddress, regAddr, NR_REGS_PER_LINE, buf );
      if ( seperator == ' ' )
      {
        PRINT_CONST_STR( F(  "0x" ) );
        PRINT_UINT_HEX( regAddr );
        PRINT_CONST_STR( F(  ": " ) );
      }
      for ( j = 0; j < NR_REGS_PER_LINE; j++ )
      {
        PRINT_CONST_STR( F(  " 0x" ) ); PRINT_UINT_HEX( *ptr++ ); PRINT_CHAR( seperator ); 
      }
      PRINT_LN( );
      regAddr = regAddr + 8;
    }
    if ( seperator == ',' )
    {
      PRINT_CONST_STR( F(  "};" ) );
      PRINT_LN( );
    }
  }
}

/******************************************************************************/
/* Application Functions                                                      */
/******************************************************************************/



/** @brief Function to print distance map in ASCII PGM (P2) format.
 *  @param distances ... pointer to array of uint16 distance values (in row-major order)
 *  @param width ... number of columns
 *  @param height ... number of rows
 */
void printDistanceMapPGM ( const uint16_t * distances, uint16_t width, uint16_t height )
{
  if ( distances == nullptr || width == 0 || height == 0 )
  {
    return;
  }

  uint32_t totalPixels = (uint32_t)width * height;
  uint16_t maxDist = 0;
  for ( uint32_t i = 0; i < totalPixels; i++ )
  {
    if ( distances[i] > maxDist )
    {
      maxDist = distances[i];
    }
  }

  uint16_t maxVal = ( maxDist > 255 ) ? maxDist : 255;

  PRINT_CONST_STR( F( "P2" ) );
  PRINT_LN( );
  PRINT_CONST_STR( F( "# TMF8829 " ) );
  PRINT_INT( width );
  PRINT_CHAR( 'x' );
  PRINT_INT( height );
  PRINT_CONST_STR( F( " Distance Map" ) );
  PRINT_LN( );
  PRINT_INT( width );
  PRINT_CHAR( ' ' );
  PRINT_INT( height );
  PRINT_LN( );
  PRINT_UINT( maxVal );
  PRINT_LN( );

  for ( uint16_t y = 0; y < height; y++ )
  {
    for ( uint16_t x = 0; x < width; x++ )
    {
      PRINT_UINT( distances[y * width + x] );
      if ( x < ( width - 1 ) )
      {
        PRINT_CHAR( ' ' );
      }
    }
    PRINT_LN( );
  }
}

/** @brief Function to decode a result frame payload and print it in ASCII PGM (P2) format.
 *  @param frame ... pointer to raw frame data buffer (including pre-header and frame header)
 *  @param frameLen ... total length of raw frame data
 */
void printFramePGM ( const uint8_t * frame, size_t frameLen )
{
  if ( frame == nullptr || frameLen < ( TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE ) )
  {
    return;
  }

  uint8_t fid = frame[TMF8829_PRE_HEADER_SIZE] & TMF8829_FID_MASK;
  if ( fid != TMF8829_FID_RESULTS )
  {
    PRINT_CONST_STR( F( "#Err,Frame is not a results frame" ) );
    PRINT_LN( );
    return;
  }

  uint8_t layout = frame[TMF8829_PRE_HEADER_SIZE + 1];
  uint8_t pixelSize = tmf8829GetPixelSize( layout );
  if ( pixelSize == 0 )
  {
    pixelSize = 3;
  }

  const size_t pixelStart = TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE;
  const size_t pixelDataBytes = ( frameLen > ( pixelStart + TMF8829_FRAME_FOOTER_SIZE ) )
                              ? ( frameLen - pixelStart - TMF8829_FRAME_FOOTER_SIZE )
                              : 0;
  uint16_t totalPixels = pixelDataBytes / pixelSize;
  if ( totalPixels == 0 )
  {
    return;
  }

  uint16_t width = 8, height = 8;
  if ( totalPixels == 64 ) { width = 8; height = 8; }
  else if ( totalPixels == 256 ) { width = 16; height = 16; }
  else if ( totalPixels == 768 ) { width = 48; height = 16; }
  else if ( totalPixels == 1536 ) { width = 48; height = 32; }

  static uint16_t distances[1536];
  memset( distances, 0, sizeof(distances) );

  for ( uint16_t i = 0; i < totalPixels && i < 1536; i++ )
  {
    size_t offset = pixelStart + ( i * pixelSize );
    size_t idx = 0;
    if ( layout & TMF8829_CFG_RESULT_FORMAT_NOISE_STRENGTH_MASK ) { idx += 2; }
    if ( layout & TMF8829_CFG_RESULT_FORMAT_XTALK_MASK ) { idx += 2; }

    if ( ( offset + idx + 2 ) <= frameLen )
    {
      uint16_t dist = (uint16_t)frame[offset + idx] | ( (uint16_t)frame[offset + idx + 1] << 8 );
      if ( tmf8829.clkCorrectionEnable )
      {
        dist = tmf8829CorrectDistance( &tmf8829, dist );
      }
      distances[i] = dist;
    }
  }

  printDistanceMapPGM( distances, width, height );
}

/** @brief Sets mode 48x32, enables measurement, reads a single full 48x32 frame (2 sub-frames),
 *  stops measurement, and prints the raw frames as hex dumps and decoded 48x32 distance matrix in PGM format.
 *  @return APP_SUCCESS_OK on success, or error code on failure.
 */
int8_t singleShot48x32HexDump ( void )
{
  if ( stateTmf8829 == TMF8829_STATE_DISABLED || stateTmf8829 == TMF8829_STATE_ERROR )
  {
    PRINT_CONST_STR( F( "Cannot perform single shot: Device is not initialized." ) );
    PRINT_LN( );
    PRINT_CONST_STR( F( "Please press 'e' to enable device and download firmware first." ) );
    PRINT_LN( );
    return APP_ERROR_NO_RESULT;
  }

  if ( stateTmf8829 == TMF8829_STATE_MEASURE )
  {
    tmf8829DisableInterrupts( &tmf8829, 0xFF );
    tmf8829StopMeasurement( &tmf8829 );
    stateTmf8829 = TMF8829_STATE_STOPPED;
  }

  PRINT_CONST_STR( F( "==================================================" ) );
  PRINT_LN( );
  PRINT_CONST_STR( F( "=== TMF8829 48x32 Single-Shot Capture & Hexdump ===" ) );
  PRINT_LN( );
  PRINT_CONST_STR( F( "==================================================" ) );
  PRINT_LN( );

  // 1. Set mode 48x32 Default
  PRINT_CONST_STR( F( "[1/4] Configuring 48x32 Default mode..." ) );
  PRINT_LN( );
  configNr = 7; // 48x32 Default mode (measCfg[7])
  preconfigure( TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_48X32 );

  // 2. Enable measurement
  PRINT_CONST_STR( F( "[2/4] Starting measurement (capturing 48x32 sub-frames)..." ) );
  PRINT_LN( );
  tmf8829ClrAndEnableInterrupts( &tmf8829, TMF8829_APP_INT_RESULTS | TMF8829_APP_INT_HISTOGRAMS );
  int8_t startStat = tmf8829StartMeasurement( &tmf8829 );
  if ( startStat != APP_SUCCESS_OK )
  {
    PRINT_CONST_STR( F( "#Err,Failed to start measurement" ) );
    PRINT_LN( );
    tmf8829DisableInterrupts( &tmf8829, 0xFF );
    stateTmf8829 = TMF8829_STATE_STOPPED;
    return startStat;
  }
  stateTmf8829 = TMF8829_STATE_MEASURE;

  // 3. Wait for 48x32 frames (2 sub-frames: even and odd rows)
  static uint16_t distances[1536];
  memset( distances, 0, sizeof(distances) );
  static uint8_t rawFrames[2][4096];
  memset( rawFrames, 0, sizeof(rawFrames) );
  size_t rawFrameSizes[2] = {0, 0};
  static uint8_t frameBuffer[4096];
  memset( frameBuffer, 0, sizeof(frameBuffer) );
  bool subFrameSeen[2] = {false, false};
  int framesCaptured = 0;
  const int timeoutMs = 15000;
  int elapsedMs = 0;
  int lastDotMs = 0;

  while ( ( !subFrameSeen[0] || !subFrameSeen[1] ) && elapsedMs < timeoutMs && framesCaptured < 4 )
  {
    uint8_t intStatus = tmf8829GetAndClrInterrupts( &tmf8829, TMF8829_APP_INT_RESULTS | TMF8829_APP_INT_HISTOGRAMS );
    uint8_t fifoStatusReg = 0xFF;
    rxReg( &tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFOSTATUS, 1, &fifoStatusReg );
    bool fifoHasData = ( ( fifoStatusReg & 0x04 ) == 0 ); // Bit 2 (txfifo_empty): 0 = data available in FIFO

    if ( ( intStatus & TMF8829_APP_INT_RESULTS ) || fifoHasData )
    {
      // Drain frames from FIFO while data is available
      while ( ( !subFrameSeen[0] || !subFrameSeen[1] ) && framesCaptured < 4 )
      {
        size_t totalFrameSize = 0;
        int8_t rxStat = rxReg( &tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFOSTATUS,
                               TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE,
                               frameBuffer );
        if ( rxStat != APP_SUCCESS_OK )
        {
          break;
        }

        uint8_t fid = frameBuffer[TMF8829_PRE_HEADER_SIZE] & TMF8829_FID_MASK;
        if ( fid != TMF8829_FID_RESULTS && fid != TMF8829_FID_HISTOGRAMS )
        {
          break;
        }

        totalFrameSize = TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE;
        uint8_t layout = frameBuffer[TMF8829_PRE_HEADER_SIZE + 1];
        uint16_t payload = tmf8829GetUint16( frameBuffer + TMF8829_PRE_HEADER_SIZE + 2 );

        uint16_t sizeToRead = 0;
        if ( payload >= ( TMF8829_FRAME_HEADER_SIZE - TMF8829_FRAME_HEADER_OFFSET ) )
        {
          sizeToRead = payload - ( TMF8829_FRAME_HEADER_SIZE - TMF8829_FRAME_HEADER_OFFSET );
        }

        if ( sizeToRead > 0 && ( totalFrameSize + sizeToRead <= sizeof(frameBuffer) ) )
        {
          uint16_t remaining = sizeToRead;
          while ( remaining > 0 )
          {
            uint16_t chunk = ( remaining > DATA_BUFFER_SIZE ) ? DATA_BUFFER_SIZE : remaining;
            rxStat = rxReg( &tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFO, chunk, frameBuffer + totalFrameSize );
            if ( rxStat != APP_SUCCESS_OK )
            {
              PRINT_CONST_STR( F( "#Err,Failed to read FIFO payload." ) );
              PRINT_LN( );
              break;
            }
            totalFrameSize += chunk;
            remaining -= chunk;
          }
        }

        if ( fid == TMF8829_FID_RESULTS )
        {
          uint8_t subResult = 0;
          if ( layout & TMF8829_RESULT_FRAME_SUBIDX_MASK )
          {
            subResult = 1;
          }
          else if ( subFrameSeen[0] && !subFrameSeen[1] )
          {
            subResult = 1;
          }
          else if ( !subFrameSeen[0] && subFrameSeen[1] )
          {
            subResult = 0;
          }

          PRINT_CONST_STR( F( " -> Captured Sub-frame " ) );
          PRINT_INT( subResult );
          PRINT_CONST_STR( F( ( subResult == 0 ) ? " (Even Rows)" : " (Odd Rows)" ) );
          PRINT_CONST_STR( F( " (Layout=0x" ) );
          PRINT_UINT_HEX( layout );
          PRINT_CONST_STR( F( ") at " ) );
          PRINT_INT( elapsedMs );
          PRINT_CONST_STR( F( " ms" ) );
          PRINT_LN( );
          uint8_t pixelSize = tmf8829GetPixelSize( layout );
          if ( pixelSize == 0 )
          {
            pixelSize = 3;
          }

          const size_t pixelStart = TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE;
          const int pixelsInSubFrame = 768; // 16 rows * 48 columns

          for ( int m = 0; m < pixelsInSubFrame; m++ )
          {
            int r = m / 48; // row in sub-frame (0..15)
            int c = m % 48; // column (0..47)
            int y = ( r * 2 ) + subResult; // full matrix row (0..31)
            int x = c;
            int fullIdx = y * 48 + x;

            size_t offset = pixelStart + ( m * pixelSize );
            if ( ( offset + 2 ) <= totalFrameSize )
            {
              size_t idx = 0;
              if ( layout & TMF8829_CFG_RESULT_FORMAT_NOISE_STRENGTH_MASK ) { idx += 2; }
              if ( layout & TMF8829_CFG_RESULT_FORMAT_XTALK_MASK ) { idx += 2; }
              if ( ( offset + idx + 2 ) <= totalFrameSize )
              {
                uint16_t dist = (uint16_t)frameBuffer[offset + idx] | ( (uint16_t)frameBuffer[offset + idx + 1] << 8 );
                if ( tmf8829.clkCorrectionEnable )
                {
                  dist = tmf8829CorrectDistance( &tmf8829, dist );
                }
                distances[fullIdx] = dist;
              }
            }
          }

          if ( subResult < 2 )
          {
            memcpy( rawFrames[subResult], frameBuffer, totalFrameSize );
            rawFrameSizes[subResult] = totalFrameSize;
          }
          subFrameSeen[subResult] = true;
          framesCaptured++;
        }

        // Check if another frame is ready in the FIFO
        fifoStatusReg = 0xFF;
        rxReg( &tmf8829, tmf8829.i2cSlaveAddress, TMF8829_COM_FIFOSTATUS, 1, &fifoStatusReg );
        if ( ( fifoStatusReg & 0x04 ) != 0 ) // Bit 2 is 1 -> FIFO is empty
        {
          break;
        }
      }
    }
    else
    {
      if ( elapsedMs - lastDotMs >= 1000 )
      {
        PRINT_CHAR( '.' );
        lastDotMs = elapsedMs;
      }
    }
    vTaskDelay( pdMS_TO_TICKS( 5 ) );
    elapsedMs += 5;
  }
  if ( lastDotMs > 0 )
  {
    PRINT_LN( );
  }

  // 4. Stop measurement
  PRINT_CONST_STR( F( "[3/4] Stopping measurement..." ) );
  PRINT_LN( );
  tmf8829DisableInterrupts( &tmf8829, 0xFF );
  tmf8829StopMeasurement( &tmf8829 );
  stateTmf8829 = TMF8829_STATE_STOPPED;

  if ( framesCaptured == 0 )
  {
    PRINT_CONST_STR( F( "#Err,Timeout waiting for frames from sensor." ) );
    PRINT_LN( );
    return APP_ERROR_NO_RESULT;
  }

  // 5. Print decoded info and Hexdumps
  PRINT_CONST_STR( F( "[4/4] Printing frame hexdumps and PGM image:" ) );
  PRINT_LN( );

  for ( int i = 0; i < 2; i++ )
  {
    if ( !subFrameSeen[i] ) { continue; }
    const uint8_t * buf = rawFrames[i];
    size_t sz = rawFrameSizes[i];
    uint8_t fid = buf[TMF8829_PRE_HEADER_SIZE] & TMF8829_FID_MASK;
    uint8_t layout = buf[TMF8829_PRE_HEADER_SIZE + 1];
    uint8_t subResult = ( layout & TMF8829_RESULT_FRAME_SUBIDX_MASK ) ? 1 : 0;
    uint16_t payload = tmf8829GetUint16( buf + TMF8829_PRE_HEADER_SIZE + 2 );
    uint32_t frameNum = tmf8829GetUint32( buf + TMF8829_PRE_HEADER_SIZE + 4 );
    uint32_t tTick = tmf8829GetUint32( buf + 1 );
    uint16_t eofMarker = ( sz >= 2 ) ? tmf8829GetUint16( buf + sz - TMF8829_FRAME_EOF_SIZE ) : 0;

    PRINT_CONST_STR( F( "--- Frame " ) ); PRINT_INT( i + 1 );
    PRINT_CONST_STR( F( " (Sub-frame " ) ); PRINT_INT( subResult );
    PRINT_CONST_STR( F( ( subResult == 0 ) ? " - Even Rows) ---" : " - Odd Rows) ---" ) );
    PRINT_LN( );
    PRINT_CONST_STR( F( " Frame Type (FID): 0x" ) ); PRINT_UINT_HEX( fid );
    if ( fid == TMF8829_FID_RESULTS ) { PRINT_CONST_STR( F( " (RESULTS)" ) ); }
    PRINT_LN( );
    PRINT_CONST_STR( F( " Layout: 0x" ) ); PRINT_UINT_HEX( layout ); PRINT_LN( );
    PRINT_CONST_STR( F( " Payload: " ) ); PRINT_INT( payload ); PRINT_CONST_STR( F( " bytes" ) ); PRINT_LN( );
    PRINT_CONST_STR( F( " Frame Number: " ) ); PRINT_UINT( frameNum ); PRINT_LN( );
    PRINT_CONST_STR( F( " SysTick: " ) ); PRINT_UINT( tTick ); PRINT_LN( );
    PRINT_CONST_STR( F( " Total Raw Frame Bytes: " ) ); PRINT_INT( sz ); PRINT_LN( );
    PRINT_CONST_STR( F( " End-of-Frame Marker: 0x" ) ); PRINT_UINT_HEX( eofMarker );
    if ( eofMarker == TMF8829_FRAME_EOF ) { PRINT_CONST_STR( F( " (OK - 0xE0F7)" ) ); }
    else { PRINT_CONST_STR( F( " (MISMATCH)" ) ); }
    PRINT_LN( );
    PRINT_LN( );

    PRINT_CONST_STR( F( "--- Raw Frame Hexdump ---" ) );
    PRINT_LN( );
    printHexDump( buf, sz );
    PRINT_LN( );
  }

  PRINT_CONST_STR( F( "--- Decoded 48x32 Frame PGM Format ---" ) );
  PRINT_LN( );
  printDistanceMapPGM( distances, 48, 32 );
  PRINT_CONST_STR( F( "==================================================" ) );
  PRINT_LN( );

  return APP_SUCCESS_OK;
}

/** @brief Compatibility wrapper for single-shot frame capture.
 */
int8_t singleShot8x8HexDump ( void )
{
  return singleShot48x32HexDump( );
}

/** @brief Function will change the pre-configuration of the Tmf8829 device.
    Next itemfrom measCfg is used.
 */
void nextConfiguration ( )
{
  if ( stateTmf8829 == TMF8829_STATE_STOPPED )
  {
    configNr = configNr + 1;
    if ( configNr >= NR_OF_MEAS_CFGS )
    {
      configNr = 0;     // wrap around
    }
    preconfigure( measCfg[configNr] );
  }
  else
  {
    PRINT_CONST_STR( F(  "Cannot change configuration: Device must be in STOPPED state." ) );
    PRINT_LN( );
    PRINT_CONST_STR( F(  "Current" ) );
    printState( );
    PRINT_CONST_STR( F(  " (Press 'e' to enable or 's' to stop measurement first)" ) );
    PRINT_LN( );
  }
}

/** @brief Function will enable/disable clock correction.
 */

void clockCorrection ( )
{
  clkCorrectionOn = !clkCorrectionOn;       // toggle clock correction on/off  
  tmf8829ClkCorrection( &(tmf8829), clkCorrectionOn );
  PRINT_CONST_STR( F(  "Clk corr is " ) );
  PRINT_INT( clkCorrectionOn );
  PRINT_LN( );
}

/** @brief Function will decrease the log Level.
 */
void logLevelDec ( )
{
  if ( logLevel > 0 )
  {
    logLevel--;
    tmf8829SetLogLevel( &tmf8829, logLevels[ logLevel ] );
  }
  PRINT_CONST_STR( F(  "Log=" ) );
  PRINT_INT( logLevels[ logLevel ] );
  PRINT_LN( );
}

/** @brief Function will increase the log Level.
 */

void logLevelInc ( )
{
  if ( logLevel < NR_LOG_LEVELS - 1 )
  {
    logLevel++;
    tmf8829SetLogLevel( &tmf8829, logLevels[ logLevel ] );
  }
  PRINT_CONST_STR( F(  "Log=" ) );
  PRINT_INT( logLevels[ logLevel ] );
  PRINT_LN( );
}

/** @brief Function will print the Arduino version,
 * firmware version, chip version and serial number.
 */
void printDeviceInfo ( )
{
  PRINT_CONST_STR( F(  "TMF8829 Arduino Driver Version " ) );
  PRINT_INT( tmf8829.info.version[0] ); PRINT_CHAR( '.' );
  PRINT_INT( tmf8829.info.version[1] ); PRINT_CHAR( '.' );
  PRINT_INT( TMF8829_APPLICATION_MINOR_VERSION );
  PRINT_LN( );
  PRINT_CONST_STR( F(  "Firmware Application Version " ) );
  PRINT_INT( tmf8829.device.appVersion[0] ); PRINT_CHAR( '.' );
  PRINT_INT( tmf8829.device.appVersion[1] ); PRINT_CHAR( '.' );
  PRINT_INT( tmf8829.device.appVersion[2] ); PRINT_CHAR( '.' );
  PRINT_INT( tmf8829.device.appVersion[3] ); PRINT_CHAR( '.' );
  PRINT_LN( );
  PRINT_CONST_STR( F(  "Chip Version " ) );
  PRINT_INT( tmf8829.device.chipVersion[0] ); PRINT_CHAR( '.' );
  PRINT_INT( tmf8829.device.chipVersion[1] ); 
  PRINT_LN( );
  PRINT_CONST_STR( F(  "Serial Number 0x" ) );
  PRINT_UINT_HEX( tmf8829.device.deviceSerialNumber );
  PRINT_LN( );
}

/** @brief Function prints the current state (stateTmf8829) in a readable format
 */
void printState ( )
{
  PRINT_CONST_STR( F(  " state=" ) );
  switch ( stateTmf8829 )
  {
    case TMF8829_STATE_DISABLED: PRINT_CONST_STR( F(  "disabled" ) ); break;
    case TMF8829_STATE_STANDBY: PRINT_CONST_STR( F(  "standby" ) ); break;
    case TMF8829_STATE_STOPPED: PRINT_CONST_STR( F(  "stopped" ) ); break;
    case TMF8829_STATE_MEASURE: PRINT_CONST_STR( F(  "measure" ) ); break;
    case TMF8829_STATE_ERROR: PRINT_CONST_STR( F(  "error" ) ); break;   
    default: PRINT_CONST_STR( F(  "???" ) ); break;
  }
  PRINT_LN( );
}

/******************************************************************************/
/* Binary Input Functions                                                     */
/******************************************************************************/
/* For communication with more than one character the Binary Input Mode must be used instead of the Character Input Mode.
  
  The supported binary commands are TMF8829_BINARY_CMD_CONFIGURE and TMF8829_BINARY_CMD_PRE_CONFIGURE.

  Note:
  The payload for these commands must exactly fit! Too long or too short commands will end in unexpected behaviour of the application.
  (If a command is too short, the application will not exit the binary mode.)
  An expected commands payload size is known with the function binaryCmdPayloadSize().

  To enter the mode the character 'b' must be sent first and the function enterBinaryInputMode() is called.
  As long as data for the command is received, the binary mode is active. The function isInBinaryInputMode() is used to know which mode is active.
  The incoming data is processed with the function handleBinaryInput(). If the right amount of data is received,
  the binary command is executed in the function handleCompleteBinaryCmd.
  The mode is left with the function exitBinaryInputMode().
 */

/** @brief This function enters the binary input mode
 */
void enterBinaryInputMode ( )
{
  binaryCmd = TMF8829_BINARY_CMD_PENDING;
  binaryBufFill = 0;
  PRINT_CONST_STR( F( "Binary input mode active" ) );
  PRINT_LN( );
}

/** @brief This function leaves the binary input mode.
 */
void exitBinaryInputMode ( )
{
  binaryCmd = TMF8829_BINARY_CMD_CHAR_MODE;
  binaryBufFill = 0;
  PRINT_CONST_STR( F( "Binary input mode inactive" ) );
  PRINT_LN( );
  printState( );
}

/** @brief This function returns the expected binary command payload size.
 *  @param cmd ... the binary command.
 * \return  expected payload size if a valid identifier is passed, -1 if the identifier is invalid
 */
int16_t binaryCmdPayloadSize( uint8_t cmd ) {
  if ( cmd == TMF8829_BINARY_CMD_CONFIGURE )
  {
    return (TMF8829_CFG_PAGE_SIZE);
  }
  else if  ( cmd == TMF8829_BINARY_CMD_PRE_CONFIGURE )
  {
    return sizeof(uint8_t);
  }
  else
  {
    PRINT_CONST_STR( F( "#Err,NoCmdPayload" ) );
    PRINT_LN( );
    return -1;
  }
}
/** @brief Function checks for binary input mode.
 * \return 1 if in binary input mode, 0 if in character input mode
*/
int8_t isInBinaryInputMode ( )
{
  if ( binaryCmd == TMF8829_BINARY_CMD_CHAR_MODE ) {
    return 0;
  } else {
    return 1;
  }
}

/** @brief This function handles a received binary command with payload of the expected size.
 * \return  1 if program termination is requested, otherwise 0
 */
int8_t handleCompleteBinaryCmd ( )
{
  if ( stateTmf8829 == TMF8829_STATE_STOPPED && binaryCmd == TMF8829_BINARY_CMD_CONFIGURE)
  {
    int8_t stat = tmf8829GetConfiguration(&tmf8829);
     
    if (stat == APP_SUCCESS_OK )
    {
      memcpy(tmf8829.config, binaryBuf, TMF8829_CFG_PAGE_SIZE);
      stat = tmf8829SetConfiguration(&tmf8829);
    }
    if (stat == APP_SUCCESS_OK )
    {
      PRINT_CONST_STR( F( "Config changed" ) );
      PRINT_CHAR( SEPARATOR );
    }
    else
    {
      stateTmf8829 = TMF8829_STATE_ERROR;
      PRINT_CONST_STR( F(  "#Err" ) );
      PRINT_CHAR( SEPARATOR );
      PRINT_CONST_STR( F(  "Config" ) );
    }
    PRINT_LN( );
  }
  else if ( stateTmf8829 == TMF8829_STATE_STOPPED && binaryCmd == TMF8829_BINARY_CMD_PRE_CONFIGURE )
  {
    uint8_t precmd = *binaryBuf;
    preconfigure(  precmd );
  }
  else
  {
    PRINT_CONST_STR( F( "Unknown Command" ) );
    PRINT_UINT_HEX( binaryCmd); 
    PRINT_CONST_STR( F( "State" ) );
    PRINT_UINT_HEX( stateTmf8829); 
    PRINT_LN( );
  }

  return 0;
}

/** @brief This function handles a single incoming byte in binary input mode.
 *  @param byte ... incoming byte
 * \return 1 if program termination is requested, otherwise 0
 */
int8_t handleBinaryInput ( uint8_t byte )
{
  if ( binaryCmd == TMF8829_BINARY_CMD_PENDING )
  {
    if ( binaryCmdPayloadSize( byte ) == -1 ) // function returns -1 if command identifier is invalid
    {
      PRINT_CONST_STR( F( "#Err,BinaryCmd," ) );
      PRINT_UINT_HEX( byte );
      PRINT_LN( );
      exitBinaryInputMode( );
    }
    else
    {
      binaryCmd = byte;
    }
  }
  else
  {
    if ( binaryBufFill >= TMF8829_BINARY_BUF_SIZE ) // prevent buffer overflow (this check is not neccessary when TMF8829_BINARY_BUF_SIZE is correctly set to the maximum payload size of all binary commands)
    {
      PRINT_CONST_STR( F( "#Err,BinaryBuf" ) );
      PRINT_LN( );
      exitBinaryInputMode( );
    }
    else
    {
      binaryBuf[binaryBufFill++] = byte; //fill the Buffer 

      if ( binaryBufFill == binaryCmdPayloadSize( binaryCmd ) )
      {
        int8_t res = handleCompleteBinaryCmd( ); // handle binary command when expected payload size has been reached
        exitBinaryInputMode( );
        return res;
      }
    }
  }

  return 0;
}

/******************************************************************************/
/* Character Input Functions                                                  */
/******************************************************************************/


/** @brief  This function handles a single incoming character in character input mode.
 * \return  1 if program termination is requested, otherwise 0
 */
int8_t handleCharInput ( char key )
{
  if ( key < 33 || key >= 126 ) // skip all control characters and DEL  
  {
    return 0; // nothing to do here
  }

  if ( key == 'h' )
  {
    printHelp(); 
  }
  else if ( key == 'c' || key == 'p' ) // show and use next configuration / profile
  {
    nextConfiguration( );
  }
  else if ( key == 'e' ) // enable and download FW
  {  
    enable( tmf8829_image_start, tmf8829_firmware, tmf8829_image_length );
  }
  else if ( key == 'd' )       // disable
  {  
    tmf8829Disable( &tmf8829 );
    stateTmf8829 = TMF8829_STATE_DISABLED;
  }
  else if ( key == 'u' )       // get configuration
  {
    getConfiguration( );
  }
  else if ( key == 'w' )       // wakeup
  {
    wakeup( );
  }
  else if ( key == 'P' )       // power down
  {
    powerDown( );
  }
  else if ( key == 'm' )
  {  
    measure( );
  }
  else if ( key == 's' )
  {
    stop( );
  }
  else if ( key == 'z' )
  {
    histogramDumping( );
  }
  else if ( key == 'a' )
  {  
    if ( stateTmf8829 != TMF8829_STATE_DISABLED )
    {
      printRegisters( 0x00, 256, ' ' );  
    }
  }
  else if ( key == 'x' )
  {
    clockCorrection( );
  }
  else if ( key == '+' ) // increase logging
  {
    logLevelInc( );
  }
  else if ( key == '-' ) // decrease logging
  {
    logLevelDec( );
  }
  else if ( key == '1' || key == 'o' || key == 'O' ) // single shot 48x32 frame hexdump
  {
    singleShot48x32HexDump( );
  }
  else if ( key == '#' ) // reset chip to test the reset function itself
  {
    reset( );
  }
  else if ( key == 'q' ) // terminate on device where this can be done
  {
    return 0; // terminate if possible
  }
  else if ( key == 'b' )       // binary mode
  {  
    enterBinaryInputMode( );
  }
  else 
  {
    PRINT_CONST_STR( F(  "#Err" ) );
    PRINT_CHAR( SEPARATOR );
    PRINT_CONST_STR( F(  "Cmd " ) );
    PRINT_CHAR( key );
    PRINT_LN( );
  }
  
  printState();
  return 0;
}

/******************************************************************************/
/* Arduino helper functions                                                   */
/******************************************************************************/

/** @brief  Function checks the UART for received characters and interprets them
 * \return  1 if program termination is requested, otherwise 0
 */
int8_t serialInput ( )
{
  char rx;
  uint32_t handledChars = 0;
  int8_t read = inputGetKey( &rx );
  while ( read ) {
    int8_t res;
    if ( isInBinaryInputMode( ) )
    {
      res = handleBinaryInput( ( uint8_t ) rx );
    }
    else
    {
      res = handleCharInput( rx );
    }
    if ( res != 0 ) {
      return res;
    }

    handledChars++;
    if ( ( handledChars & 0x0F ) == 0 )
    {
      TickType_t ticks = pdMS_TO_TICKS( 1 );
      if ( ticks == 0 ) {
        ticks = 1;
      }
      vTaskDelay( ticks );
    }

    read = inputGetKey( &rx );
  }
  return 0;     // rx must be 0 to leave while loop
}

/** @brief This function resets the status of the application
 */
void resetAppState ( )
{
  stateTmf8829 = TMF8829_STATE_DISABLED;
  configNr = NR_OF_MEAS_CFGS;        // reset of preconfigure Nr.
  clkCorrectionOn = 1;
  irqTriggered = 0;
}

/** @brief Interrupt handler is called when INT pin goes low.
 */
void interruptHandler ( void )
{
  irqTriggered = 1;
}

/******************************************************************************/
/* Initial setup logic, before entering the main loop                         */
/******************************************************************************/

void initial_setup( uint8_t logLevelIdx, uint32_t baudrate, uint32_t spiClockSpeedInHz )
{
  logLevel = logLevelIdx;

  configurePins( &tmf8829 );

  // start serial and spi
  inputOpen( baudrate );
  spiOpen( &tmf8829, spiClockSpeedInHz );
  resetAppState( );
  tmf8829Initialise( &tmf8829 );
  tmf8829SetLogLevel( &tmf8829, logLevels[ logLevelIdx ] );
  setInterruptHandler( interruptHandler );
  tmf8829Disable( &tmf8829 );                                     // this resets the I2C address in the device
  delayInMicroseconds(CAP_DISCHARGE_TIME_MS * 1000); // wait for a proper discharge of the cap
  printHelp();
}

int8_t main_loop ( )
{
  int8_t res = APP_SUCCESS_OK;
  uint8_t intStatus = 0;
  int8_t exit = serialInput();   // handle any keystrokes from UART

#if ( defined( USE_INTERRUPT_TO_TRIGGER_READ ) && (USE_INTERRUPT_TO_TRIGGER_READ != 0) )
  if ( irqTriggered )
  {
    disableInterrupts( );
    irqTriggered = 0;
    enableInterrupts( );

#else
  if ( stateTmf8829 == TMF8829_STATE_MEASURE )
  { 
#endif
    intStatus = tmf8829GetAndClrInterrupts( &tmf8829, TMF8829_APP_INT_RESULTS | TMF8829_APP_INT_HISTOGRAMS );

    if ( intStatus & TMF8829_APP_INT_RESULTS )   // check if a result is available
    {
      res = tmf8829ReadResults( &tmf8829 );
    }
    if ( intStatus & TMF8829_APP_INT_HISTOGRAMS )
    {
      res = tmf8829ReadHistogram( &tmf8829);
    }

  }

  if ( res != APP_SUCCESS_OK ) // in case that fails there is some error in programming or on the device, this should not happen
  {
    tmf8829DisableInterrupts( &tmf8829, 0xFF );
    tmf8829StopMeasurement( &tmf8829 );
    stateTmf8829 = TMF8829_STATE_STOPPED;
    PRINT_CONST_STR( F(  "#Err" ) );
    PRINT_CHAR( SEPARATOR );
    PRINT_CONST_STR( F(  "inter" ) );
    PRINT_CHAR( SEPARATOR );
    PRINT_INT( intStatus );
    PRINT_CHAR( SEPARATOR );
    PRINT_CONST_STR( F(  "but no data" ) );
    PRINT_LN( );
  }
  return !exit;    // 1 == loop again, 0 == exit
}

void final_clean_shutdown ( )
{
  tmf8829Disable( &tmf8829 );
  clrInterruptHandler( );

  spiClose( &tmf8829 );
  inputClose( );
}
