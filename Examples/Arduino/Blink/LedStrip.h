#ifndef LED_STRIP_H
#define LED_STRIP_H

#include <Arduino.h>

typedef struct
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
} rgb_t;

/**
 * @brief WS2812 LED strip driver based on ESP32 RMT peripheral
 * Support global brightness adjustment and single LED independent brightness
 * Automatically allocate RMT waveform buffer memory in constructor
 * Automatically free memory in destructor to avoid memory leak
 */
class LedStrip {
private:
    uint8_t m_brightness;    // Global brightness value, range 0 ~ 255
    int m_pin;                 // RMT TX output pin number
    uint16_t m_numLeds;       // Total number of LEDs on the strip
    rmt_data_t *m_led_data;   // Pointer to dynamically allocated RMT waveform buffer

public:
    /**
     * @brief Constructor of LedStrip class
     * Automatically allocate memory for RMT waveform buffer
     * @param pin RMT transmit GPIO pin
     * @param num_leds Total LED count of the strip
     */
    LedStrip(int pin, uint16_t num_leds);

    /**
     * @brief Destructor of LedStrip class
     * Release dynamically allocated RMT buffer memory
     */
    ~LedStrip();

    /**
     * @brief Initialize ESP32 RMT transmit peripheral
     */
    void init();

    /**
     * @brief Check if RMT waveform buffer memory allocation succeeded
     * @return true memory ready, false memory allocation failed
     */
    bool isMemReady() const;

    /**
     * @brief Set global brightness for all LEDs
     * @param brightness Brightness value, clamp to 0 ~ 255
     */
    void setBrightness(uint8_t brightness);

    /**
     * @brief Get current global brightness value
     * @return uint8_t current brightness 0 ~ 255
     */
    uint8_t getBrightness() const;

    /**
     * @brief Set color of single LED with global brightness
     * @param index Target LED index
     * @param r Red channel raw value 0~255
     * @param g Green channel raw value 0~255
     * @param b Blue channel raw value 0~255
     */
    void setColor(uint16_t index, uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Set color of single LED with independent brightness (ignore global brightness)
     * @param index Target LED index
     * @param r Red channel raw value 0~255
     * @param g Green channel raw value 0~255
     * @param b Blue channel raw value 0~255
     * @param bright Independent brightness for this LED, clamp to 0~255
     */
    void setColor(uint16_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t bright);

    /**
     * @brief Set color of single LED with independent brightness (ignore global brightness)
     * @param index Target LED index
     * @param rgb Red Green Blue channel raw value 0~255
     */
    void setColor(uint16_t index, rgb_t rgb);

    /**
     * @brief Send cached RMT waveform data to LED strip to refresh display
     */
    void show();
};

#endif
