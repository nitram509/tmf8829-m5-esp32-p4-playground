#include <cstdio>
#include <cstring>
#include "tmf8829/tmf8829.h"
#include "app.h"
#include "help.h"

#define TMF8829_APPLICATION_MINOR_VERSION    5


/** @brief Helper to get descriptive name of a pre-configuration command.
 */
const char * getPreconfigName ( uint8_t cfgCmd )
{
  switch ( cfgCmd )
  {
    case TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_8X8:
      return "8x8 Default";
    case TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_8X8_LONG_RANGE:
      return "8x8 Long Range (12m)";
    case TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_8X8_HIGH_ACCURACY:
      return "8x8 High Accuracy (Short Range)";
    case TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_16X16:
      return "16x16 Default";
    case TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_16X16_HIGH_ACCURACY:
      return "16x16 High Accuracy (Short Range)";
    case TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_32X32:
      return "32x32 Default";
    case TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_32X32_HIGH_ACCURACY:
      return "32x32 High Accuracy (Short Range)";
    case TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_48X32:
      return "48x32 Default";
    case TMF8829_CMD_STAT__cmd_stat__CMD_LOAD_CFG_48X32_HIGH_ACCURACY:
      return "48x32 High Accuracy (Short Range)";
    default:
      return "Custom/Unknown";
  }
}

/** @brief Helper to get descriptive name of focal plane mode.
 */
const char * getFpModeName ( uint8_t fpMode )
{
  switch ( fpMode )
  {
    case TMF8829_CFG_FP_MODE_8x8A:
      return "8x8 A";
    case TMF8829_CFG_FP_MODE_8x8B:
      return "8x8 B";
    case TMF8829_CFG_FP_MODE_16x16:
      return "16x16";
    case TMF8829_CFG_FP_MODE_32x32:
      return "32x32";
    case TMF8829_CFG_FP_MODE_32x32s:
      return "32x32 short";
    case TMF8829_CFG_FP_MODE_48x32:
      return "48x32";
    default:
      return "Unknown";
  }
}

/** @brief  Function prints a help screen.
 */
void printHelp ( )
{
  PRINT_CONST_STR( F(  "TMF8829" ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "a ..... dump registers" ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "c / p . next configuration / profile" ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "d ..... disable device" ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "e ..... enable device and download TMF8829 FW" ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "h ..... help " ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "m ..... measure" ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "P ..... power down (standby)" ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "s ..... stop measure" ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "u ..... get configuration" ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "w ..... wakeup" ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "x ..... clock corr on/off" ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "z ..... histogram" ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "+ ..... log+" ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "- ..... log-" ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "1 / o . single-shot 48x32 frame & PGM" ) );
  PRINT_LN( ); PRINT_CONST_STR( F(  "# ..... reset" ) );
  PRINT_LN( );
}

/** @brief Function will print the Arduino version,
 * firmware version, chip version and serial number.
 */
void printDeviceInfo() {
  PRINT_CONST_STR(F( "TMF8829 Arduino Driver Version " ));
  PRINT_INT(tmf8829.info.version[0]);
  PRINT_CHAR('.');
  PRINT_INT(tmf8829.info.version[1]);
  PRINT_CHAR('.');
  PRINT_INT(TMF8829_APPLICATION_MINOR_VERSION);
  PRINT_LN();
  PRINT_CONST_STR(F( "Firmware Application Version " ));
  PRINT_INT(tmf8829.device.appVersion[0]);
  PRINT_CHAR('.');
  PRINT_INT(tmf8829.device.appVersion[1]);
  PRINT_CHAR('.');
  PRINT_INT(tmf8829.device.appVersion[2]);
  PRINT_CHAR('.');
  PRINT_INT(tmf8829.device.appVersion[3]);
  PRINT_CHAR('.');
  PRINT_LN();
  PRINT_CONST_STR(F( "Chip Version " ));
  PRINT_INT(tmf8829.device.chipVersion[0]);
  PRINT_CHAR('.');
  PRINT_INT(tmf8829.device.chipVersion[1]);
  PRINT_LN();
  PRINT_CONST_STR(F( "Serial Number 0x" ));
  PRINT_UINT_HEX(tmf8829.device.deviceSerialNumber);
  PRINT_LN();
}

/** @brief Function prints the current state (stateTmf8829) in a readable format
 */
void printState() {
  PRINT_CONST_STR(F( " state=" ));
  switch (stateTmf8829) {
    case TMF8829_STATE_DISABLED: PRINT_CONST_STR(F( "disabled" ));
      break;
    case TMF8829_STATE_STANDBY: PRINT_CONST_STR(F( "standby" ));
      break;
    case TMF8829_STATE_STOPPED: PRINT_CONST_STR(F( "stopped" ));
      break;
    case TMF8829_STATE_MEASURE: PRINT_CONST_STR(F( "measure" ));
      break;
    case TMF8829_STATE_ERROR: PRINT_CONST_STR(F( "error" ));
      break;
    default: PRINT_CONST_STR(F( "???" ));
      break;
  }
  PRINT_LN();
}
