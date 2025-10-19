#include "sps30_manager.h"

bool SPS30Manager::initialized = false;
bool SPS30Manager::connected = false;
String SPS30Manager::lastError = "";
SPS30 SPS30Manager::sps30;

bool SPS30Manager::init()
{
    if (initialized)
    {
        return true;
    }

    // Configure pins
    sps30.SetSerialPin(SPS30Manager::SPS30_RX_PIN, SPS30Manager::SPS30_TX_PIN);
    initialized = true;
    return true;
}

bool SPS30Manager::begin()
{
    if (!initialized)
    {
        if (!init())
        {
            return false;
        }
    }

    // Try to establish communication
    if (!initCommunication())
    {
        return false;
    }

    // Setup the sensor
    if (!setupSensor())
    {
        return false;
    }

    connected = true;
    return true;
}

bool SPS30Manager::initCommunication()
{
    clearError();

    for (uint8_t attempt = 0; attempt < MAX_RETRIES; attempt++)
    {
        if (sps30.begin(SP30_COMMS))
        {
            if (sps30.probe())
            {
                return true;
            }
            setError("Could not probe SPS30");
        }
        else
        {
            setError("Could not initialize SPS30 communication");
        }

        if (attempt < MAX_RETRIES - 1)
        {
            delay(RETRY_DELAY_MS);
        }
    }

    return false;
}

bool SPS30Manager::setupSensor()
{
    clearError();

    // Reset the sensor
    for (uint8_t attempt = 0; attempt < MAX_RETRIES; attempt++)
    {
        if (sps30.reset())
        {
            break;
        }
        setError("Could not reset SPS30");

        if (attempt < MAX_RETRIES - 1)
        {
            delay(RETRY_DELAY_MS);
        }
        else
        {
            return false;
        }
    }

    // Start measurements
    for (uint8_t attempt = 0; attempt < MAX_RETRIES; attempt++)
    {
        if (sps30.start())
        {
            return true;
        }
        setError("Could not start SPS30 measurements");

        if (attempt < MAX_RETRIES - 1)
        {
            delay(RETRY_DELAY_MS);
        }
    }

    return false;
}

bool SPS30Manager::measure(struct sps_values &values)
{
    if (!connected)
    {
        setError("SPS30 not connected");
        return false;
    }

    clearError();
    uint8_t errorCount = 0;

    do
    {
        uint8_t ret = sps30.GetValues(&values);

        // Handle different error cases
        if (ret == SPS30_ERR_OK)
        {
            return true;
        }
        else if (ret == SPS30_ERR_DATALENGTH)
        {
            // Data not ready, normal condition
            if (errorCount++ > MAX_RETRIES)
            {
                setError("Max retries exceeded waiting for data", ret);
                return false;
            }
            delay(100);
            continue;
        }
        else
        {
            char buf[80];
            sps30.GetErrDescription(ret, buf, 80);
            setError(buf, ret);
            return false;
        }
    } while (true);
}

void SPS30Manager::stop()
{
    if (connected)
    {
        sps30.stop();
        connected = false;
    }
}

void SPS30Manager::setError(const char *error, uint8_t errorCode)
{
    if (errorCode)
    {
        char buf[80];
        sps30.GetErrDescription(errorCode, buf, 80);
        lastError = String(error) + String(" (") + String(buf) + String(")");
    }
    else
    {
        lastError = error;
    }
}

void SPS30Manager::clearError()
{
    lastError = "";
}