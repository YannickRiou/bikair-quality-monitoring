#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>

//-----------------------------------------------------------------------------
// System Configuration
//-----------------------------------------------------------------------------
// CPU and Power Management
static const uint32_t CPU_FREQUENCY = 160;         // CPU frequency in MHz
static const uint64_t uS_TO_S_FACTOR = 1000000ULL; // Microseconds to seconds conversion
static const uint64_t TIME_TO_SLEEP = 10;          // Sleep duration in seconds
static const uint32_t BOOT_DELAY_MS = 100;         // Boot stability delay

//-----------------------------------------------------------------------------
// Network Configuration
//-----------------------------------------------------------------------------
static const char *WIFI_SSID = "bikeair";
static const char *WIFI_PASSWORD = "madlicorne1234";
static const IPAddress LOCAL_IP(192, 168, 1, 66);
static const IPAddress GATEWAY(192, 168, 1, 1);
static const IPAddress SUBNET(255, 255, 255, 0);
static const char *HOSTNAME = "bikair";

//-----------------------------------------------------------------------------
// Hardware Configuration
//-----------------------------------------------------------------------------
// I2C Configuration
static const uint8_t I2C_SDA = 21;
static const uint8_t I2C_SCL = 22;
static const uint32_t I2C_FREQUENCY = 100000; // 100 kHz for stability

// UART Configuration
static const uint8_t GPS_RX = GPIO_NUM_4;
static const uint8_t GPS_TX = GPIO_NUM_32;
static const uint8_t SPS30_TX = 9;
static const uint8_t SPS30_RX = 10;
static const uint32_t GPS_BAUD_RATE = 9600;
static const uint32_t SPS30_BAUD_RATE = 115200;

// GPIO Configuration
static const uint8_t LED_PIN = GPIO_NUM_2;

//-----------------------------------------------------------------------------
// Sensor Configuration
//-----------------------------------------------------------------------------
// Timing Parameters
static const uint32_t FIX_TIMEOUT = 150000;          // GPS fix timeout (ms)
static const uint32_t MEASUREMENT_DURATION = 10000;  // Measurement cycle (ms)
static const uint32_t NUMBER_OF_MEASUREMENTS = 5;    // Measurements per cycle
static const uint32_t DEFAULT_MEASURE_PERIOD = 2000; // Default period (ms)
static const uint32_t MIN_MEASURE_PERIOD = 1000;     // Minimum period (ms)
static const uint32_t MAX_MEASURE_PERIOD = 10000;    // Maximum period (ms)
static const uint32_t DEFAULT_MANUAL_INTERVAL = 2;   // Default manual interval (seconds)
static const uint32_t MIN_MANUAL_INTERVAL = 1;       // Minimum manual interval (seconds)
static const uint32_t MAX_MANUAL_INTERVAL = 60;      // Maximum manual interval (seconds)

// Sample Sizes for Running Medians
static const uint8_t TEMPERATURE_SAMPLES = 10;
static const uint8_t HUMIDITY_SAMPLES = 10;
static const uint8_t CO2_SAMPLES = 5;
static const uint8_t TVOC_SAMPLES = 5;
static const uint8_t SPEED_SAMPLES = 3;

//-----------------------------------------------------------------------------
// Storage Configuration
//-----------------------------------------------------------------------------
static const size_t MAX_FILES = 3;            // Maximum number of files
static const size_t MAX_FILE_SIZE = 100000;   // Maximum file size (bytes)
static const size_t MIN_FREE_SPACE = 10000;   // Minimum free space (bytes)
static const size_t MAX_TOTAL_SPACE = 300000; // Maximum total space to use (bytes)

#endif // CONFIG_H