#include "LedStrip.h"
#include <Arduino.h>

/**
 * @brief Constructor of LedStrip class
 * Automatically allocate memory for RMT waveform buffer
 * @param pin RMT transmit GPIO pin
 * @param num_leds Total LED count of the strip
 */
LedStrip::LedStrip(int pin, uint16_t num_leds)
    : m_pin(pin), m_numLeds(num_leds), m_brightness(255), m_led_data(nullptr)
{
    // Calculate total required rmt_data_t elements: each LED occupies 24 rmt_data_t
    size_t totalElements = static_cast<size_t>(m_numLeds) * 24;
    // Dynamically allocate buffer memory
    m_led_data = static_cast<rmt_data_t*>(malloc(totalElements * sizeof(rmt_data_t)));
    memset(m_led_data, 0, totalElements * sizeof(rmt_data_t));
}

/**
 * @brief Destructor of LedStrip class
 * Release dynamically allocated RMT buffer memory
 */
LedStrip::~LedStrip()
{
    if (m_led_data != nullptr)
    {
        free(m_led_data);
        m_led_data = nullptr;
    }
}

/**
 * @brief Initialize ESP32 RMT transmit peripheral
 */
void LedStrip::init()
{
    // RMT init config: TX mode, 1 memory block, clock frequency 10MHz
    bool initSuccess = rmtInit(m_pin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
    if (!initSuccess)
    {
        Serial.println("LedStrip init failed");
    }
}

/**
 * @brief Check if RMT waveform buffer memory allocation succeeded
 * @return true memory ready, false memory allocation failed
 */
bool LedStrip::isMemReady() const
{
    return m_led_data != nullptr;
}

/**
 * @brief Set global brightness for all LEDs
 * @param brightness Brightness value, clamp to 0 ~ 255
 */
void LedStrip::setBrightness(uint8_t brightness)
{
    if (brightness > 255)
    {
        brightness = 255;
    }
    m_brightness = brightness;
}

/**
 * @brief Get current global brightness value
 * @return uint8_t current brightness 0 ~ 255
 */
uint8_t LedStrip::getBrightness() const
{
    return m_brightness;
}

/**
 * @brief Set color of single LED with global brightness
 * @param index Target LED index
 * @param r Red channel raw value 0~255
 * @param g Green channel raw value 0~255
 * @param b Blue channel raw value 0~255
 */
void LedStrip::setColor(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    // Skip operation if memory invalid or LED index out of range
    if (!isMemReady() || index >= m_numLeds)
    {
        return;
    }

    uint8_t color[3];
    // Apply global brightness scaling to RGB channels
    color[0] = static_cast<uint16_t>(g) * m_brightness / 255;
    color[1] = static_cast<uint16_t>(r) * m_brightness / 255;
    color[2] = static_cast<uint16_t>(b) * m_brightness / 255;

    int dataOffset = index * 24;
    // Generate RMT waveform bit data for G/R/B 3 channels
    for (int col = 0; col < 3; col++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            // Extract single bit from MSB to LSB
            if (color[col] & (1 << (7 - bit)))
            {
                // Bit = 1 waveform timing
                m_led_data[dataOffset].level0 = 1;
                m_led_data[dataOffset].duration0 = 8;
                m_led_data[dataOffset].level1 = 0;
                m_led_data[dataOffset].duration1 = 4;
            }
            else
            {
                // Bit = 0 waveform timing
                m_led_data[dataOffset].level0 = 1;
                m_led_data[dataOffset].duration0 = 4;
                m_led_data[dataOffset].level1 = 0;
                m_led_data[dataOffset].duration1 = 8;
            }
            dataOffset++;
        }
    }
}

/**
 * @brief Set color of single LED with independent brightness (ignore global brightness)
 * @param index Target LED index
 * @param r Red channel raw value 0~255
 * @param g Green channel raw value 0~255
 * @param b Blue channel raw value 0~255
 * @param bright Independent brightness for this LED, clamp to 0~255
 */
void LedStrip::setColor(uint16_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t bright)
{
    // Skip operation if memory invalid or LED index out of range
    if (!isMemReady() || index >= m_numLeds)
    {
        return;
    }

    uint8_t br = bright;
    if (br > 255)
    {
        br = 255;
    }

    uint8_t color[3];
    // Apply independent brightness scaling to RGB channels
    color[0] = static_cast<uint16_t>(g) * br / 255;
    color[1] = static_cast<uint16_t>(r) * br / 255;
    color[2] = static_cast<uint16_t>(b) * br / 255;

    int dataOffset = index * 24;
    // Generate RMT waveform bit data for G/R/B 3 channels
    for (int col = 0; col < 3; col++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            if (color[col] & (1 << (7 - bit)))
            {
                m_led_data[dataOffset].level0 = 1;
                m_led_data[dataOffset].duration0 = 8;
                m_led_data[dataOffset].level1 = 0;
                m_led_data[dataOffset].duration1 = 4;
            }
            else
            {
                m_led_data[dataOffset].level0 = 1;
                m_led_data[dataOffset].duration0 = 4;
                m_led_data[dataOffset].level1 = 0;
                m_led_data[dataOffset].duration1 = 8;
            }
            dataOffset++;
        }
    }
}

/**
* @brief Set color of single LED with independent brightness (ignore global brightness)
* @param index Target LED index
* @param rgb Red Green Blue channel raw value 0~255
*/
void LedStrip::setColor(uint16_t index, rgb_t rgb)
{
  setColor(index,rgb.r,rgb.g,rgb.b);
}

/**
 * @brief Send cached RMT waveform data to LED strip to refresh display
 */
void LedStrip::show()
{
    // Transmit all waveform data via RMT, block until transmission complete
    rmtWrite(m_pin, m_led_data, m_numLeds * 24, RMT_WAIT_FOR_EVER);
}
