/**************************************************************************************************
* Copyright © 2024 ams-OSRAM AG                                                                   *
* All rights are reserved.                                                                        *
*                                                                                                 *
* FOR FULL LICENSE TEXT SEE LICENSES-MIT.TXT                                                      *
*                                                                                                 *
**************************************************************************************************/

#include "tmf8829_shim.h"
#include "tmf8829.h"

#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static spi_device_handle_t s_tmf8829_spi = nullptr;
static bool s_spi_bus_initialized = false;
static uint32_t s_printed_chars_since_yield = 0;

static constexpr uint32_t PRINT_YIELD_CHAR_THRESHOLD = 128;

static int8_t spiToI2cStatus(esp_err_t err)
{
    if (err == ESP_OK) {
        return I2C_SUCCESS;
    }
    if (err == ESP_ERR_TIMEOUT) {
        return I2C_ERR_TIMEOUT;
    }
    return I2C_ERR_OTHER;
}

static bool spiReady()
{
    return s_tmf8829_spi != nullptr;
}

static inline void yieldAfterPrintBytes(size_t bytes)
{
    if (bytes == 0U) {
        return;
    }

    s_printed_chars_since_yield += static_cast<uint32_t>(bytes);
    if (s_printed_chars_since_yield < PRINT_YIELD_CHAR_THRESHOLD) {
        return;
    }

    s_printed_chars_since_yield = 0;
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        TickType_t ticks = pdMS_TO_TICKS(1);
        if (ticks == 0) {
            ticks = 1;
        }
        vTaskDelay(ticks);
    }
}


void delayInMicroseconds(uint32_t wait)
{
    if (wait == 0U) {
        return;
    }

    const uint32_t waitMs = wait / 1000U;
    if (waitMs > 0U) {
        TickType_t ticks = pdMS_TO_TICKS(waitMs);
        if (ticks == 0) {
            ticks = 1;
        }
        vTaskDelay(ticks);
    }

    const uint32_t remainderUs = wait % 1000U;
    if (remainderUs > 0U) {
        esp_rom_delay_us(remainderUs);
    }
}

uint32_t getSysTick(void)
{
    return (uint32_t)esp_timer_get_time();
}

uint8_t readProgramMemoryByte(uint32_t address)
{
    return *(const uint8_t *)((uintptr_t)address);
}

void enablePinHigh(void *dptr)
{
    (void)dptr;
    gpio_set_level((gpio_num_t)ENABLE_PIN, 1);
}

void enablePinLow(void *dptr)
{
    (void)dptr;
    gpio_set_level((gpio_num_t)ENABLE_PIN, 0);
}

void configurePins(void *dptr)
{
    (void)dptr;
    pinOutput((uint8_t)ENABLE_PIN);
    pinInput(INTERRUPT_PIN);
    pinInput(TRIGGER_INTERRUPT_PIN);
}

void spiOpen(void *dptr, uint32_t spiClockSpeedInHz)
{
    (void)dptr;
    if (spiReady()) {
        return;
    }
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = CONFIG_TMF8829_PIN_MOSI;
    bus_cfg.miso_io_num = CONFIG_TMF8829_PIN_MISO;
    bus_cfg.sclk_io_num = CONFIG_TMF8829_PIN_SCLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4096; // Allow transfers up to 4 KB (covering DATA_BUFFER_SIZE)
    const spi_host_device_t host = static_cast<spi_host_device_t>(CONFIG_TMF8829_SPI_HOST);
    esp_err_t err = spi_bus_initialize(host, &bus_cfg, SPI_DMA_CH_AUTO); // Enable auto DMA channel
    if (err == ESP_ERR_INVALID_STATE) {
        s_spi_bus_initialized = true;
    } else if (err != ESP_OK) {
        return;
    } else {
        s_spi_bus_initialized = true;
    }
    const int requested_hz = (spiClockSpeedInHz == 0U) ? CONFIG_TMF8829_SPI_CLOCK_HZ : (int)spiClockSpeedInHz;
    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.clock_speed_hz = requested_hz;
    dev_cfg.mode = 0;
    dev_cfg.spics_io_num = CONFIG_TMF8829_PIN_CS;
    dev_cfg.queue_size = 1;
    err = spi_bus_add_device(host, &dev_cfg, &s_tmf8829_spi);
    if (err != ESP_OK) {
        s_tmf8829_spi = nullptr;
    }
}

void spiClose(void *dptr)
{
    (void)dptr;

    if (s_tmf8829_spi != nullptr) {
        spi_bus_remove_device(s_tmf8829_spi);
        s_tmf8829_spi = nullptr;
    }

    if (s_spi_bus_initialized) {
        const spi_host_device_t host = static_cast<spi_host_device_t>(CONFIG_TMF8829_SPI_HOST);
        spi_bus_free(host);
        s_spi_bus_initialized = false;
    }
}

void printChar(char c)
{
    putchar(c);
    yieldAfterPrintBytes(1U);
}

void printInt(int32_t i)
{
    const int printed = printf("%" PRId32, i);
    if (printed > 0) {
        yieldAfterPrintBytes(static_cast<size_t>(printed));
    }
}

void printUint(uint32_t i)
{
    const int printed = printf("%" PRIu32, i);
    if (printed > 0) {
        yieldAfterPrintBytes(static_cast<size_t>(printed));
    }
}

void printUintHex(uint32_t i)
{
    const int printed = printf("%" PRIX32, i);
    if (printed > 0) {
        yieldAfterPrintBytes(static_cast<size_t>(printed));
    }
}

void printStr(char *str)
{
    const int printed = printf("%s", str);
    if (printed > 0) {
        yieldAfterPrintBytes(static_cast<size_t>(printed));
    }
}

void printLn(void)
{
    putchar('\n');
    yieldAfterPrintBytes(1U);
}

void inputOpen(uint32_t baudrate)
{
    (void)baudrate;
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags != -1) {
        fcntl(STDIN_FILENO, F_SETFL, flags);
    }
}

void inputClose(void)
{
}

int8_t inputGetKey(char *c)
{
    if (c == nullptr) {
        return 0;
    }
    char ch = 0;
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n > 0) {
        *c = ch;
        return 1;
    }
    *c = 0;
    return 0;
}

void printConstStr(const char *str)
{
    const int printed = printf("%s", str);
    if (printed > 0) {
        yieldAfterPrintBytes(static_cast<size_t>(printed));
    }
}

void pinOutput(uint8_t pin)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .hys_ctrl_mode = GPIO_HYS_SOFT_DISABLE,
    };
    gpio_config(&io_conf);
}

void pinInput(uint8_t pin)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .hys_ctrl_mode = GPIO_HYS_SOFT_DISABLE,
    };
    gpio_config(&io_conf);
}

void setInterruptHandler(void (*handler)(void))
{
    (void)handler;
}

void clrInterruptHandler(void)
{
}

void disableInterrupts(void)
{
}

void enableInterrupts(void)
{
}

int8_t txReg(void *dptr, uint8_t slaveAddr, uint8_t regAddr, uint16_t toTx, const uint8_t *txData)
{
    return spiTxReg(dptr, slaveAddr, regAddr, toTx, txData);
}

int8_t rxReg(void *dptr, uint8_t slaveAddr, uint8_t regAddr, uint16_t toRx, uint8_t *rxData)
{
    return spiRxReg(dptr, slaveAddr, regAddr, toRx, rxData);
}

int8_t spiTxReg(void *dptr, uint8_t slaveAddr, uint8_t regAddr, uint16_t toTx, const uint8_t *txData)
{
    (void)dptr;
    (void)slaveAddr;
    if (!spiReady()) {
        return I2C_ERR_OTHER;
    }
    if ((toTx > 0U) && (txData == nullptr)) {
        return I2C_ERR_OTHER;
    }
    if (toTx > DATA_BUFFER_SIZE) {
        return I2C_ERR_DATA_TOO_LONG;
    }

    uint8_t ioBuffer[DATA_BUFFER_SIZE + 2] = {0};
    ioBuffer[0] = SPI_WR_CMD;
    ioBuffer[1] = regAddr;
    if (toTx > 0U) {
        memcpy(&ioBuffer[2], txData, toTx);
    }

    spi_transaction_t trans = {};
    trans.length = (toTx + 2U) * 8U;
    trans.tx_buffer = ioBuffer;
    const esp_err_t err = spi_device_polling_transmit(s_tmf8829_spi, &trans);
    return spiToI2cStatus(err);
}

int8_t spiRxReg(void *dptr, uint8_t slaveAddr, uint8_t regAddr, uint16_t toRx, uint8_t *rxData)
{
    (void)dptr;
    (void)slaveAddr;
    if (!spiReady()) {
        return I2C_ERR_OTHER;
    }
    if ((toRx > 0U) && (rxData == nullptr)) {
        return I2C_ERR_OTHER;
    }
    if (toRx > DATA_BUFFER_SIZE) {
        return I2C_ERR_DATA_TOO_LONG;
    }

    const size_t totalLen = 3U + toRx;
    uint8_t txBuf[DATA_BUFFER_SIZE + 3] = {0};
    uint8_t rxBuf[DATA_BUFFER_SIZE + 3] = {0};

    txBuf[0] = SPI_RD_CMD;
    txBuf[1] = regAddr;
    txBuf[2] = 0x00; // Dummy / status byte

    spi_transaction_t trans = {};
    trans.length = totalLen * 8U;
    trans.rxlength = totalLen * 8U;
    trans.tx_buffer = txBuf;
    trans.rx_buffer = rxBuf;

    esp_err_t err = spi_device_polling_transmit(s_tmf8829_spi, &trans);
    if (err != ESP_OK) {
        return spiToI2cStatus(err);
    }

    if (toRx > 0U) {
        memcpy(rxData, &rxBuf[3], toRx);
    }
    return I2C_SUCCESS;
}

int8_t i2cTxRx(void *dptr, uint8_t slaveAddr, uint16_t toTx, const uint8_t *txData, uint16_t toRx, uint8_t *rxData)
{
    int8_t res = I2C_SUCCESS;
    if (toTx > 0U) {
        if (txData == nullptr) {
            return I2C_ERR_OTHER;
        }
        res = spiTxReg(dptr, slaveAddr, txData[0], toTx - 1U, (toTx > 1U) ? (txData + 1U) : nullptr);
    }
    if (toRx > 0U && res == I2C_SUCCESS) {
        if (rxData == nullptr) {
            return I2C_ERR_OTHER;
        }
        res = spiRxReg(dptr, slaveAddr, 0, toRx, rxData);
    }
    return res;
}

void handleReceivedFrameHeaderData ( void * dptr, uint8_t * data )
{
  tmf8829Driver * driver = (tmf8829Driver *)dptr;  
  if ( driver->logLevel == TMF8829_LOG_LEVEL_RESULTS_HEADER )
  {
    printResultHeader( driver, data, TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE );
  }
  if ( driver->logLevel > TMF8829_LOG_LEVEL_RESULTS_HEADER )
  {
    PRINT_STR( "#Obj " );
    printResults( driver, data, TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE );
   
  }
}

void handleReceivedResultData ( void * dptr, uint8_t * data, uint16_t size )
{
  tmf8829Driver * driver = (tmf8829Driver *)dptr;  
  (void)size;
  if ( driver->logLevel >= TMF8829_LOG_LEVEL_RESULTS )
  {
    printResults( driver, data, size ); 
  }
  // Note: data size ==1 Error for EOF check, change buffer size !
}

void handleReceivedHistogramData( void * dptr, uint8_t * data, uint16_t size )
{
  tmf8829Driver * driver = (tmf8829Driver *)dptr;
  (void)size;
  if ( driver->logLevel >= TMF8829_LOG_LEVEL_RESULTS )
  {
    printHistogram( driver, data, size );
  }
  // Note: data size ==1 Error for EOF check, change buffer size !
}

void handleReceivedResultDataEnd( void * dptr )
{
  tmf8829Driver * driver = (tmf8829Driver *)dptr;

  if (driver->logLevel >= TMF8829_LOG_LEVEL_RESULTS_HEADER )
  {
    PRINT_LN( );
  }
}

void handleReceivedHistogramDataEnd( void * dptr )
{
  tmf8829Driver * driver = (tmf8829Driver *)dptr;

  if (driver->logLevel >= TMF8829_LOG_LEVEL_RESULTS_HEADER )
  {
    PRINT_LN( );
  }
}

// Result Header printing:
// #Obj,<i2c_slave_address>,<fifostatus>,<systick>,
//      <frame_identifier>,<result_layout>,<payload>,<frameNumber>,
//      <temperature0>,<temperature1>,<temperature2>,<bdv_value>,<ref_peak_position1>, <ref_peak_position2>
void printResultHeader ( void * dptr, uint8_t * data, uint8_t len )
{
  tmf8829Driver * driver = (tmf8829Driver *)dptr;  
  if ( len == (TMF8829_PRE_HEADER_SIZE + TMF8829_FRAME_HEADER_SIZE) )
  {
    uint32_t sysTick = tmf8829GetUint32( data + 1 );
    uint16_t payload = tmf8829GetUint16( data + TMF8829_PRE_HEADER_SIZE + 2 );
    uint32_t frameNum = tmf8829GetUint32( data + TMF8829_PRE_HEADER_SIZE + 4 );
    uint16_t refPos1 = tmf8829GetUint16( data + TMF8829_PRE_HEADER_SIZE + 12 );
    uint16_t refPos2 = tmf8829GetUint16( data + TMF8829_PRE_HEADER_SIZE + 14 );
    PRINT_STR( "#Obj" );
    PRINT_CHAR( SEPARATOR );
    PRINT_UINT( driver->i2cSlaveAddress );
    PRINT_CHAR( SEPARATOR );
    PRINT_UINT( data[ 0 ] );
    PRINT_CHAR( SEPARATOR );
    PRINT_UINT( sysTick );
    PRINT_CHAR( SEPARATOR );
    PRINT_UINT( data[ TMF8829_PRE_HEADER_SIZE ] ); //ID
    PRINT_CHAR( SEPARATOR );
    PRINT_UINT( data[ TMF8829_PRE_HEADER_SIZE + 1 ] ); // Layout
    PRINT_CHAR( SEPARATOR );
    PRINT_UINT( payload ); // Payload
    PRINT_CHAR( SEPARATOR );
    PRINT_UINT( frameNum ); // Frame Number
    PRINT_CHAR( SEPARATOR );
    PRINT_UINT( data[ TMF8829_PRE_HEADER_SIZE + 8 ] ); // Temp0
    PRINT_CHAR( SEPARATOR );
    PRINT_UINT( data[ TMF8829_PRE_HEADER_SIZE + 9 ] ); // Temp1
    PRINT_CHAR( SEPARATOR );
    PRINT_UINT( data[ TMF8829_PRE_HEADER_SIZE + 10 ] ); // Temp2
    PRINT_CHAR( SEPARATOR );
    PRINT_UINT( data[ TMF8829_PRE_HEADER_SIZE + 11 ] ); // BDV
    PRINT_CHAR( SEPARATOR );
    PRINT_UINT( refPos1 ); // Ref Pos 1
    PRINT_CHAR( SEPARATOR );
    PRINT_UINT( refPos2 ); // Ref Pos 2
  }
  else // result structure too short
  {
    PRINT_STR( "#Err" );
    PRINT_CHAR( SEPARATOR );
    PRINT_STR( "header size length wrong" );
    PRINT_CHAR( SEPARATOR );
    PRINT_INT( len );
    PRINT_LN( );
  }
}

void printResults ( void * dptr, uint8_t * data, uint16_t len )
{
  (void) dptr; // not used for this platform
  uint16_t cnt;
  
  for ( cnt = 0 ; cnt < len ; cnt ++ )
  {
    PRINT_INT( data[cnt] );
    PRINT_CHAR( SEPARATOR );
  }
}

void printHistogram ( void * dptr, uint8_t * data, uint16_t len )
{
  (void) dptr; // not used for this platform
  uint16_t cnt;

  for ( cnt = 0 ; cnt < len ; cnt ++ )
  {
    PRINT_INT( data[cnt] );
    PRINT_CHAR( SEPARATOR );
  }

}
