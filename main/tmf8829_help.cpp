#include <cstdio>
#include <cstring>
#include "tmf8829.h"
#include "tmf8829_help.h"

#define DELAY_PRINT_HELP  10000 /**< delay between the prints */


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
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_CONST_STR( F(  "TMF8829" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "a ..... dump registers" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "c / p . next configuration / profile" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "d ..... disable device" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "e ..... enable device and download TMF8829 FW" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "h ..... help " ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "m ..... measure" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "P ..... power down (standby)" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "s ..... stop measure" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "u ..... get configuration" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "w ..... wakeup" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "x ..... clock corr on/off" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "z ..... histogram" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "+ ..... log+" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "- ..... log-" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "1 / o . single-shot 48x32 frame hexdump & PGM" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); PRINT_CONST_STR( F(  "# ..... reset" ) );
  delayInMicroseconds( DELAY_PRINT_HELP );
  PRINT_LN( ); 
}

/** @brief Function to print buffer contents as formatted canonical hex dump.
 *  @param data ... pointer to byte array
 *  @param len ... length of byte array
 */
void printHexDump ( const uint8_t * data, size_t len )
{
  if ( data == nullptr || len == 0 )
  {
    return;
  }
  for ( size_t i = 0; i < len; i += 16 )
  {
    char lineBuf[128];
    int pos = snprintf( lineBuf, sizeof(lineBuf), "%08X: ", (unsigned int)i );

    // Hex bytes
    for ( size_t j = 0; j < 16; j++ )
    {
      if ( j == 8 && pos < (int)sizeof(lineBuf) )
      {
        lineBuf[pos++] = ' ';
      }
      if ( ( i + j ) < len )
      {
        pos += snprintf( lineBuf + pos, sizeof(lineBuf) - pos, "%02X ", data[i + j] );
      }
      else
      {
        pos += snprintf( lineBuf + pos, sizeof(lineBuf) - pos, "   " );
      }
    }

    // ASCII characters
    if ( pos < (int)sizeof(lineBuf) )
    {
      pos += snprintf( lineBuf + pos, sizeof(lineBuf) - pos, " |" );
    }
    for ( size_t j = 0; j < 16 && ( i + j ) < len && pos < (int)sizeof(lineBuf); j++ )
    {
      uint8_t c = data[i + j];
      lineBuf[pos++] = ( c >= 32 && c <= 126 ) ? (char)c : '.';
    }
    if ( pos < (int)sizeof(lineBuf) )
    {
      lineBuf[pos++] = '|';
    }
    lineBuf[pos] = '\0';
    PRINT_STR( lineBuf );
    PRINT_LN( );
  }
}