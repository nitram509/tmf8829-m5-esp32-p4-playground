#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "tmf8829.h"
#include "tmf8829_shim.h"
#include "tmf8829_help.h"
#include "tmf8829_app.h"

#define UART_BAUD_RATE 2000000

static const char *TAG = "tmf8829_app";

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
    tmf8829Disable( &tmf8829 );                                          // this resets the I2C address in the device
    delayInMicroseconds( CAP_DISCHARGE_TIME_MS * 1000 );                 // wait for a proper discharge of the cap
    printHelp( );
}

int8_t main_loop( void )
{
    int8_t res = APP_SUCCESS_OK;
    uint8_t intStatus = 0;
    int8_t exit = serialInput( );                                        // handle any keystrokes from UART

#if ( defined( USE_INTERRUPT_TO_TRIGGER_READ ) && ( USE_INTERRUPT_TO_TRIGGER_READ != 0 ) )
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

        if ( intStatus & TMF8829_APP_INT_RESULTS )                       // check if a result is available
        {
            res = tmf8829ReadResults( &tmf8829 );
        }
        if ( intStatus & TMF8829_APP_INT_HISTOGRAMS )
        {
            res = tmf8829ReadHistogram( &tmf8829 );
        }
    }

    if ( res != APP_SUCCESS_OK )                                         // in case that fails there is some error in programming or on the device, this should not happen
    {
        tmf8829DisableInterrupts( &tmf8829, 0xFF );
        tmf8829StopMeasurement( &tmf8829 );
        stateTmf8829 = TMF8829_STATE_STOPPED;
        PRINT_CONST_STR( F( "#Err" ) );
        PRINT_CHAR( SEPARATOR );
        PRINT_CONST_STR( F( "inter" ) );
        PRINT_CHAR( SEPARATOR );
        PRINT_INT( intStatus );
        PRINT_CHAR( SEPARATOR );
        PRINT_CONST_STR( F( "but no data" ) );
        PRINT_LN( );
    }
    return !exit;                                                        // 1 == loop again, 0 == exit
}

void final_clean_shutdown( void )
{
    tmf8829Disable( &tmf8829 );
    clrInterruptHandler( );

    spiClose( &tmf8829 );
    inputClose( );
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting TMF8829 Arduino application flow");
    initial_setup(1, UART_BAUD_RATE, CONFIG_TMF8829_SPI_CLOCK_HZ);
    while (main_loop()) {
        TickType_t delay_ticks = pdMS_TO_TICKS(10);
        if (delay_ticks == 0) {
            delay_ticks = 1;
        }
        vTaskDelay(delay_ticks);
    }
    final_clean_shutdown();
}
