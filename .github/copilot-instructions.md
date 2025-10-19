---
description: "Instructions for writing C++ code for ESP32 embedded systems using Arduino framework with performance and memory constraints"
applyTo: "**/*.cpp,**/*.hpp,**/*.h,**/*.ino"
---

# C++ ESP32 Development Instructions

Follow efficient C++ practices optimized for embedded systems development on ESP32 targets using the Arduino framework. These instructions prioritize memory efficiency, execution performance, and real-time constraints typical of microcontroller environments.

## General Instructions

- Write efficient, deterministic C++ code optimized for embedded systems
- Prioritize memory efficiency and predictable execution times
- Minimize dynamic allocations and prefer stack allocation
- Use RAII (Resource Acquisition Is Initialization) principles
- Leverage compile-time optimizations and const correctness
- Avoid standard library features that are expensive or unavailable
- Consider real-time constraints and interrupt safety
- Use Arduino framework conventions while maintaining C++ best practices
- If pointers are needed, always test that they are not NULL
- Prefer using std::vector or std::array
- Always use smart pointers (std::shared_ptr or std::unique_ptr or std::weak_ptr)

### Functions

 - Write simple, focused functions—each should do one thing
 - Break complex logic into smaller, specialized functions
 - Keep functions short for clarity and maintainability
 - Prefer using const reference as function and method argument
 - Input argument should be const
 - Return early to minimize nesting and keep the happy path left-aligned
 - Use descriptive names that clearly state the function’s purpose
 - Document exported functions with comments starting with the function name
 - Place a comment above each function explaining what it does.
 - For every function, clearly document the expected input and output using explicit sections labeled 'Input:' and 'Output:' in the comment. Example:
	 /*
	 * functionName does X.
	 *
	 * Input:
	 *   param1: type, description
	 *   param2: type, description
	 *
	 * Output:
	 *   returnType: description
	 */

## Memory Management

### Dynamic Memory

- **Avoid `new`/`delete` and `malloc`/`free` in production code**
- Use stack allocation whenever possible
- Pre-allocate buffers at startup if dynamic memory is necessary
- Use object pools for frequently created/destroyed objects
- Monitor heap fragmentation using `ESP.getFreeHeap()`
- Prefer placement new for controlled memory allocation

```cpp
// Good: Stack allocation
char buffer[256];
MyClass instance;

// Bad: Dynamic allocation
char* buffer = new char[256];  // Avoid
MyClass* instance = new MyClass();  // Avoid

// Acceptable: Pre-allocated at startup
char* globalBuffer = nullptr;
void setup() {
    globalBuffer = (char*)malloc(BUFFER_SIZE);
    // Check allocation success
}
```

### Memory Optimization

- Use appropriate data types (uint8_t instead of int for small values)
- Pack structures to minimize memory footprint
- Use const data in PROGMEM for string literals and lookup tables
- Minimize global variables and prefer local scope
- Use bit fields for boolean flags

```cpp
// Good: Memory-efficient structure
struct __attribute__((packed)) SensorData {
    uint16_t temperature : 12;  // 0-4095 range
    uint16_t humidity : 12;     // 0-4095 range
    uint8_t status : 4;         // 0-15 flags
    uint8_t reserved : 4;
};

// Good: PROGMEM usage for constants
const char PROGMEM welcomeMsg[] = "System Ready";
const uint16_t PROGMEM lookupTable[] = {100, 200, 300, 400};
```

## Performance Optimization

### Execution Efficiency

- Use inline functions for small, frequently called functions
- Leverage constexpr for compile-time computations
- Avoid floating-point operations when possible
- Use bit operations instead of division/multiplication by powers of 2
- Minimize function call overhead in interrupt handlers
- Cache frequently accessed values

```cpp
// Good: Compile-time computation
constexpr uint32_t BAUD_RATE_DIVIDER = F_CPU / (16UL * 115200UL);

// Good: Bit operations
inline uint8_t multiplyBy8(uint8_t value) {
    return value << 3;  // Instead of value * 8
}

// Good: Efficient interrupt handler
void IRAM_ATTR buttonISR() {
    // Minimal code, set flag only
    buttonPressed = true;
}
```

### Real-Time Considerations

- Keep interrupt service routines (ISR) short and fast
- Use volatile for variables accessed in ISRs
- Disable interrupts only when absolutely necessary
- Use appropriate task priorities with FreeRTOS
- Implement watchdog timer feeds in long-running loops

```cpp
volatile bool sensorDataReady = false;
volatile uint16_t sensorValue = 0;

void IRAM_ATTR sensorISR() {
    sensorValue = analogRead(SENSOR_PIN);
    sensorDataReady = true;
}
```

## Naming Conventions

### Variables and Functions

- Use camelCase for variables and functions
- Use PascalCase for classes and types
- Use UPPER_CASE for constants and macros
- Use descriptive names even if they're longer
- Prefix member variables with 'm'
- Use 'k' prefix for compile-time constants

```cpp
// Good naming examples
class SensorManager {
private:
    uint8_t mSensorCount;    
public:
    void initializeSensors();
    uint16_t readTemperature();
};

constexpr uint8_t kMaxSensors = 8;
const uint32_t WIFI_TIMEOUT_MS = 5000;
```

### Pin and Hardware Definitions

- Use descriptive names for pin assignments
- Group related pins in namespaces or classes
- Use const or constexpr for pin definitions
- Document pin functions and electrical characteristics

```cpp
// Good: Hardware abstraction
namespace Pins {
    constexpr uint8_t LED_STATUS = 2;
    constexpr uint8_t BUTTON_INPUT = 0;
    constexpr uint8_t SDA_PIN = 21;
    constexpr uint8_t SCL_PIN = 22;
    constexpr uint8_t SENSOR_POWER = 4;
}

// Good: Hardware configuration class
class HardwareConfig {
public:
    static constexpr uint8_t UART_TX = 1;
    static constexpr uint8_t UART_RX = 3;
    static constexpr uint32_t UART_BAUD = 115200;
};
```

## Error Handling and Safety

### Error Handling Patterns

- Use return codes or status enums instead of exceptions
- Implement timeout mechanisms for blocking operations
- Validate inputs and hardware states
- Use assertions for debug builds
- Implement graceful degradation for non-critical failures

```cpp
enum class Status : uint8_t {
    OK = 0,
    ERROR_TIMEOUT,
    ERROR_INVALID_PARAM,
    ERROR_HARDWARE_FAULT
};

Status initializeWiFi(uint32_t timeoutMs) {
    uint32_t startTime = millis();
    
    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > timeoutMs) {
            return Status::ERROR_TIMEOUT;
        }
        delay(100);
        yield(); // Feed watchdog
    }
    
    return Status::OK;
}
```

### Safety and Robustness

- Initialize all variables
- Check return values from Arduino functions
- Implement bounds checking for arrays
- Use const correctness throughout
- Validate hardware states before operations

```cpp
// Good: Safe array access
class CircularBuffer {
private:
    static constexpr size_t kBufferSize = 32;
    uint8_t buffer_[kBufferSize];
    size_t head_ = 0;
    size_t tail_ = 0;
    
public:
    bool push(uint8_t value) {
        size_t nextHead = (head_ + 1) % kBufferSize;
        if (nextHead == tail_) {
            return false; // Buffer full
        }
        buffer_[head_] = value;
        head_ = nextHead;
        return true;
    }
};
```

## Arduino Framework Integration

### Setup and Loop Structure

- Keep setup() function focused on initialization
- Design loop() to be non-blocking
- Use state machines for complex sequences
- Implement proper timing without using delay()
- Handle serial communication efficiently

```cpp
enum class SystemState : uint8_t {
    INITIALIZING,
    RUNNING,
    ERROR,
    SLEEP
};

SystemState currentState = SystemState::INITIALIZING;
uint32_t lastSensorRead = 0;
constexpr uint32_t SENSOR_INTERVAL_MS = 1000;

void setup() {
    Serial.begin(115200);
    
    // Initialize hardware
    pinMode(Pins::LED_STATUS, OUTPUT);
    
    // Initialize sensors
    if (initializeSensors() != Status::OK) {
        currentState = SystemState::ERROR;
        return;
    }
    
    currentState = SystemState::RUNNING;
}

void loop() {
    switch (currentState) {
        case SystemState::RUNNING:
            handleRunningState();
            break;
        case SystemState::ERROR:
            handleErrorState();
            break;
        default:
            break;
    }
}

void handleRunningState() {
    if (millis() - lastSensorRead >= SENSOR_INTERVAL_MS) {
        readSensors();
        lastSensorRead = millis();
    }
    
    // Handle other tasks
    processSerialInput();
    updateDisplay();
}
```

### Library Usage

- Prefer Arduino-specific libraries for hardware interfaces
- Use ESP32-specific features when beneficial
- Avoid heavy C++ standard library components
- Wrap complex libraries in simpler interfaces
- Document library dependencies and versions

```cpp
// Good: Wrapper for complex library
class WiFiManager {
private:
    bool isConnected_ = false;
    uint32_t lastConnectionCheck_ = 0;
    static constexpr uint32_t CONNECTION_CHECK_INTERVAL = 5000;
    
public:
    Status initialize() {
        WiFi.mode(WIFI_STA);
        return connectToNetwork();
    }
    
    void update() {
        if (millis() - lastConnectionCheck_ >= CONNECTION_CHECK_INTERVAL) {
            checkConnection();
            lastConnectionCheck_ = millis();
        }
    }
    
    bool isConnected() const { return isConnected_; }
};
```

## Concurrency and Interrupts

### FreeRTOS Integration

- Use FreeRTOS tasks for concurrent operations
- Implement proper task synchronization
- Use appropriate stack sizes for tasks
- Handle task communication safely
- Monitor stack usage in debug builds

```cpp
// Good: Task implementation
TaskHandle_t sensorTaskHandle = nullptr;
QueueHandle_t sensorDataQueue = nullptr;

void sensorTask(void* parameter) {
    SensorData data;
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(100); // 10Hz
    
    while (true) {
        // Read sensors
        data.temperature = readTemperatureSensor();
        data.humidity = readHumiditySensor();
        data.timestamp = millis();
        
        // Send to queue (non-blocking)
        xQueueSend(sensorDataQueue, &data, 0);
        
        vTaskDelayUntil(&lastWakeTime, frequency);
    }
}

void createTasks() {
    sensorDataQueue = xQueueCreate(10, sizeof(SensorData));
    
    xTaskCreate(
        sensorTask,
        "SensorTask",
        2048, // Stack size
        nullptr,
        1, // Priority
        &sensorTaskHandle
    );
}
```

### Interrupt Safety

- Use IRAM_ATTR for interrupt handlers
- Keep ISRs minimal and fast
- Use FreeRTOS ISR-safe functions
- Avoid floating-point in ISRs
- Use proper volatile declarations

```cpp
volatile bool dataReady = false;
portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR dataReadyISR() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Signal task from ISR
    vTaskNotifyGiveFromISR(dataProcessorTask, &xHigherPriorityTaskWoken);
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// Thread-safe access to shared data
void setDataReady(bool ready) {
    portENTER_CRITICAL(&dataMux);
    dataReady = ready;
    portEXIT_CRITICAL(&dataMux);
}
```

## Hardware Abstraction

### Peripheral Interfaces

- Create abstraction layers for hardware peripherals
- Use dependency injection for testability
- Implement timeout mechanisms for hardware operations
- Handle hardware failure gracefully
- Document electrical requirements and limitations

```cpp
// Good: Hardware abstraction
class I2CSensor {
private:
    uint8_t address_;
    TwoWire* wire_;
    
public:
    I2CSensor(uint8_t address, TwoWire* wire = &Wire) 
        : address_(address), wire_(wire) {}
    
    Status initialize() {
        wire_->begin();
        
        // Test communication
        wire_->beginTransmission(address_);
        uint8_t error = wire_->endTransmission();
        
        return (error == 0) ? Status::OK : Status::ERROR_HARDWARE_FAULT;
    }
    
    Status readRegister(uint8_t reg, uint8_t& value) {
        wire_->beginTransmission(address_);
        wire_->write(reg);
        uint8_t error = wire_->endTransmission();
        
        if (error != 0) {
            return Status::ERROR_HARDWARE_FAULT;
        }
        
        wire_->requestFrom(address_, 1U);
        if (wire_->available()) {
            value = wire_->read();
            return Status::OK;
        }
        
        return Status::ERROR_TIMEOUT;
    }
};
```

### Pin Management

- Create pin management classes
- Implement proper initialization sequences
- Use const correctness for pin assignments
- Document pin multiplexing and conflicts
- Implement pin state validation

```cpp
class DigitalPin {
private:
    uint8_t pin_;
    uint8_t mode_;
    bool isInitialized_ = false;
    
public:
    explicit DigitalPin(uint8_t pin) : pin_(pin) {}
    
    Status initialize(uint8_t mode) {
        if (pin_ >= NUM_DIGITAL_PINS) {
            return Status::ERROR_INVALID_PARAM;
        }
        
        pinMode(pin_, mode);
        mode_ = mode;
        isInitialized_ = true;
        return Status::OK;
    }
    
    bool read() const {
        assert(isInitialized_);
        return digitalRead(pin_);
    }
    
    void write(bool value) {
        assert(isInitialized_);
        assert(mode_ == OUTPUT);
        digitalWrite(pin_, value);
    }
};
```

## Communication Protocols

### Serial Communication

- Implement robust serial protocols
- Use non-blocking serial operations
- Implement proper framing and error detection
- Handle buffer overflows gracefully
- Use appropriate baud rates for reliability

```cpp
class SerialProtocol {
private:
    HardwareSerial* serial_;
    CircularBuffer rxBuffer_;
    uint32_t lastActivity_ = 0;
    static constexpr uint32_t TIMEOUT_MS = 1000;
    
public:
    explicit SerialProtocol(HardwareSerial* serial) : serial_(serial) {}
    
    void update() {
        // Read available data
        while (serial_->available()) {
            uint8_t byte = serial_->read();
            if (!rxBuffer_.push(byte)) {
                // Buffer overflow - handle error
                handleBufferOverflow();
            }
            lastActivity_ = millis();
        }
        
        // Check for timeout
        if (millis() - lastActivity_ > TIMEOUT_MS) {
            handleTimeout();
        }
    }
    
    bool sendPacket(const uint8_t* data, size_t length) {
        if (length == 0) return false;
        
        // Simple framing: STX + length + data + checksum + ETX
        serial_->write(0x02); // STX
        serial_->write(static_cast<uint8_t>(length));
        
        uint8_t checksum = 0;
        for (size_t i = 0; i < length; i++) {
            serial_->write(data[i]);
            checksum ^= data[i];
        }
        
        serial_->write(checksum);
        serial_->write(0x03); // ETX
        
        return true;
    }
};
```
## Testing and Debugging

### Embedded Testing Strategies

- Use hardware-in-the-loop testing when possible
- Implement serial debug output with compile-time switches
- Create test fixtures for hardware components
- Use dependency injection for testable code
- Implement built-in self-tests (BIST)

```cpp
// Good: Compile-time debug control
#ifdef DEBUG_ENABLED
    #define DEBUG_PRINT(x) Serial.print(x)
    #define DEBUG_PRINTLN(x) Serial.println(x)
    #define DEBUG_PRINTF(format, ...) Serial.printf(format, __VA_ARGS__)
#else
    #define DEBUG_PRINT(x)
    #define DEBUG_PRINTLN(x)
    #define DEBUG_PRINTF(format, ...)
#endif

// Good: Testable hardware interface
class TestableI2CDevice {
private:
    I2CInterface* interface_;
    
public:
    explicit TestableI2CDevice(I2CInterface* interface) 
        : interface_(interface) {}
    
    Status readData(uint8_t& data) {
        return interface_->readRegister(DATA_REGISTER, data);
    }
};

// Good: Built-in self-test
class SensorArray {
public:
    Status performSelfTest() {
        DEBUG_PRINTLN("Starting sensor self-test...");
        
        for (uint8_t i = 0; i < sensorCount_; i++) {
            if (testSensor(i) != Status::OK) {
                DEBUG_PRINTF("Sensor %d failed self-test\n", i);
                return Status::ERROR_HARDWARE_FAULT;
            }
        }
        
        DEBUG_PRINTLN("All sensors passed self-test");
        return Status::OK;
    }
};
```

### Performance Monitoring

- Monitor stack usage in tasks
- Track heap usage and fragmentation
- Measure execution times for critical paths
- Implement performance counters
- Use ESP32 performance monitoring features

```cpp
class PerformanceMonitor {
private:
    uint32_t loopCount_ = 0;
    uint32_t maxLoopTime_ = 0;
    uint32_t totalLoopTime_ = 0;
    uint32_t lastReport_ = 0;
    
public:
    void startLoop() {
        loopStartTime_ = micros();
    }
    
    void endLoop() {
        uint32_t loopTime = micros() - loopStartTime_;
        
        loopCount_++;
        totalLoopTime_ += loopTime;
        
        if (loopTime > maxLoopTime_) {
            maxLoopTime_ = loopTime;
        }
        
        // Report every 10 seconds
        if (millis() - lastReport_ >= 10000) {
            reportStatistics();
            lastReport_ = millis();
        }
    }
    
private:
    uint32_t loopStartTime_ = 0;
    
    void reportStatistics() {
        uint32_t avgLoopTime = totalLoopTime_ / loopCount_;
        
        DEBUG_PRINTF("Loop stats: avg=%luus, max=%luus, count=%lu\n",
                    avgLoopTime, maxLoopTime_, loopCount_);
        DEBUG_PRINTF("Free heap: %u bytes\n", ESP.getFreeHeap());
        
        // Reset counters
        loopCount_ = 0;
        maxLoopTime_ = 0;
        totalLoopTime_ = 0;
    }
};
```

## Configuration Management

### Compile-Time Configuration

- Use constexpr for configuration constants
- Implement feature flags with preprocessor directives
- Create configuration classes for related settings
- Use appropriate data types for configuration values
- Document configuration dependencies

```cpp
// Good: Configuration management
class SystemConfig {
public:
    // Network configuration
    static constexpr uint32_t WIFI_TIMEOUT_MS = 10000;
    static constexpr uint16_t MQTT_PORT = 1883;
    static constexpr uint32_t MQTT_KEEPALIVE_S = 60;
    
    // Sensor configuration
    static constexpr uint8_t MAX_SENSORS = 8;
    static constexpr uint32_t SENSOR_READ_INTERVAL_MS = 1000;
    static constexpr uint16_t SENSOR_TIMEOUT_MS = 500;
    
    // Performance configuration
    static constexpr size_t TASK_STACK_SIZE = 2048;
    static constexpr uint8_t SENSOR_TASK_PRIORITY = 2;
    static constexpr uint8_t NETWORK_TASK_PRIORITY = 1;
    
    // Feature flags
    #ifdef ENABLE_OTA_UPDATES
        static constexpr bool OTA_ENABLED = true;
    #else
        static constexpr bool OTA_ENABLED = false;
    #endif
};
```

### Runtime Configuration

- Store configuration in EEPROM or SPIFFS
- Implement configuration validation
- Provide default values for all settings
- Implement configuration backup and restore
- Use structured data for complex configurations

```cpp
struct NetworkConfig {
    char ssid[32];
    char password[64];
    char mqttServer[64];
    uint16_t mqttPort;
    uint8_t checksum;
    
    uint8_t calculateChecksum() const {
        uint8_t sum = 0;
        const uint8_t* data = reinterpret_cast<const uint8_t*>(this);
        for (size_t i = 0; i < sizeof(*this) - sizeof(checksum); i++) {
            sum ^= data[i];
        }
        return sum;
    }
    
    bool isValid() const {
        return checksum == calculateChecksum();
    }
};

class ConfigManager {
private:
    static constexpr uint16_t CONFIG_ADDRESS = 0;
    NetworkConfig config_;
    
public:
    Status loadConfiguration() {
        EEPROM.get(CONFIG_ADDRESS, config_);
        
        if (!config_.isValid()) {
            DEBUG_PRINTLN("Invalid config, using defaults");
            setDefaults();
            return saveConfiguration();
        }
        
        return Status::OK;
    }
    
    Status saveConfiguration() {
        config_.checksum = config_.calculateChecksum();
        EEPROM.put(CONFIG_ADDRESS, config_);
        EEPROM.commit();
        return Status::OK;
    }
    
private:
    void setDefaults() {
        strncpy(config_.ssid, "DefaultSSID", sizeof(config_.ssid));
        strncpy(config_.password, "", sizeof(config_.password));
        strncpy(config_.mqttServer, "mqtt.example.com", sizeof(config_.mqttServer));
        config_.mqttPort = 1883;
    }
};
```

## Common Pitfalls to Avoid

### Memory-Related Issues

- **Stack overflow**: Monitor task stack usage and allocate sufficient stack
- **Heap fragmentation**: Avoid frequent dynamic allocation/deallocation
- **Memory leaks**: Always pair allocations with deallocations
- **Buffer overruns**: Always validate array bounds and string lengths
- **Uninitialized variables**: Initialize all variables, especially in ISRs

### Timing and Concurrency Issues

- **Blocking delays**: Use non-blocking timing patterns instead of delay()
- **Race conditions**: Protect shared data with proper synchronization
- **Priority inversion**: Use appropriate task priorities and mutexes
- **Watchdog resets**: Feed the watchdog in long-running operations
- **Interrupt conflicts**: Avoid long-running code in ISRs

### Hardware and Power Issues

- **Power consumption**: Implement proper sleep modes and power management
- **GPIO conflicts**: Document and validate pin assignments
- **Electrical limits**: Respect current and voltage limitations
- **Signal integrity**: Use proper pull-up/pull-down resistors
- **Clock stability**: Consider crystal tolerances for timing-critical applications

### Arduino Framework Specific

- **String objects**: Avoid Arduino String class in production (causes fragmentation)
- **Serial buffer overflow**: Check Serial.available() before reading
- **Pin mode conflicts**: Always set pin modes before use
- **Library conflicts**: Be aware of timer and interrupt usage by libraries
- **Reset causes**: Handle different reset causes appropriately

```cpp
// Good: Avoid common pitfalls
void safeSerialRead() {
    // Check availability before reading
    if (Serial.available()) {
        char data = Serial.read();
        // Process data
    }
}

// Good: Non-blocking timing
uint32_t lastAction = 0;
const uint32_t ACTION_INTERVAL = 1000;

void loop() {
    if (millis() - lastAction >= ACTION_INTERVAL) {
        performAction();
        lastAction = millis();
    }
    
    // Other non-blocking tasks
    yield(); // Feed watchdog
}

// Good: Safe string handling
void safeCopyString(char* dest, const char* src, size_t destSize) {
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0'; // Ensure null termination
}
```

By following these guidelines, you'll create robust, efficient C++ code optimized for ESP32 embedded systems while maintaining good software engineering practices and leveraging
