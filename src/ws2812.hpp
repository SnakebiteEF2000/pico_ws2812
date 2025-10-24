#pragma once
#include <cstdint>
#include <vector>
#include "hardware/pio.h"

namespace ws {

/**
 * @brief 8-bit RGB color (0..255 per channel).
 */
struct RGB { uint8_t r, g, b; };

/**
 * @brief 8-bit RGBW color (0..255 per channel). W is ignored on plain WS2812.
 */
struct RGBW { uint8_t r, g, b, w; };

/**
 * @brief RP2040 WS2812/WS2812B (RGB) and SK6812 (RGBW) driver using Pico SDK + PIO.
 *
 * Key points:
 * - Uses one PIO state machine; 800 kHz default (configurable).
 * - Optional DMA (on by default). Falls back to blocking if no channel is free.
 * - Internal logic stores RGBW; W is dropped for RGB strips.
 * - Global brightness (0..255) and optional ~2.2 gamma correction.
 *
 * Threading:
 * - Not re-entrant. Don’t call from multiple contexts simultaneously.
 */
class Strip {
public:
    /**
     * @brief Construct a strip driver (no hardware setup yet).
     * @param pin   GPIO connected to the strip's DIN.
     * @param count Number of LEDs (valid indices: 0..count-1).
     * @param rgbw  true for SK6812 (GRBW, 32-bit), false for WS2812(B) (GRB, 24-bit).
     * @param freq  Data rate in Hz (default 800 kHz). 400 kHz works for some legacy strips.
     * @param pio   PIO block (pio0/pio1).
     * @param sm    State machine index; -1 to auto-claim a free one.
     *
     * Call begin() before use.
     */
    Strip(uint pin, uint count, bool rgbw=false, float freq=800000.0f,
          PIO pio=pio0, int sm=-1);

    /**
     * @brief Initialize PIO (and DMA if enabled).
     * @return true on success, false if no SM available or program can't be loaded.
     *
     * Configures pin directions, sideset, shifting, FIFO join, clock divider, etc.
     * Clears the internal buffer but does not send anything (call show()).
     */
    bool begin();

    /**
     * @brief Release SM/DMA (optional). Call begin() again to re-initialize.
     */
    void end();

    /// @brief Number of LEDs.
    inline uint size() const { return _count; }

    /**
     * @brief Set the entire internal buffer to off (black). Does not send.
     * @see show()
     */
    void clear();

    /**
     * @brief Set all pixels (RGB) in the buffer.
     * @param c Color (R,G,B). W remains 0 on RGBW strips.
     */
    void setAll(RGB c);

    /**
     * @brief Set all pixels (RGBW) in the buffer.
     * @param c Color (R,G,B,W). W is ignored on WS2812.
     */
    void setAll(RGBW c);

    /**
     * @brief Set one pixel (RGB) in the buffer. Out-of-range indices are ignored.
     * @param i Pixel index [0..size()-1].
     * @param c Color.
     */
    void setPixel(uint i, RGB c);

    /// @brief Set one pixel (RGB) in the buffer.
    void setPixel(uint i, uint8_t r, uint8_t g, uint8_t b);

    /// @brief Set one pixel (RGBW) in the buffer.
    void setPixel(uint i, RGBW c);

    /**
     * @brief Global brightness [0..255]. Applied when sending.
     */
    void setBrightness(uint8_t b);

    /**
     * @brief Enable/disable ~2.2 gamma correction (sRGB-ish) via an 8-bit LUT.
     */
    void enableGamma(bool on);

    /**
     * @brief Send the current buffer (blocking). Includes ≥80 µs reset latch.
     *
     * Uses DMA if available, otherwise fills the PIO TX FIFO directly.
     * Returns only after the frame is fully transmitted and the reset latch elapsed.
     */
    void show();

    /**
     * @brief Start sending asynchronously (non-blocking).
     *
     * Important: call wait() or poll busy() before starting another frame or
     * making disruptive changes. A persistent internal staging buffer is used
     * to hold the packed 24/32-bit words while DMA/PIO transfers.
     */
    void showAsync();

    /**
     * @brief true while a transfer is in flight (DMA/PIO).
     */
    bool busy() const;

    /**
     * @brief Block until the transfer finishes and the reset latch elapsed.
     */
    void wait();

    /**
     * @brief Utility: HSV → RGB (H: 0..360, S/V: 0..1).
     */
    static RGB hsv(float h, float s, float v);

private:
    /// Build packed GRB/GRBW 32-bit words for transmission.
    void build_frame(std::vector<uint32_t>& out);

    /// Start DMA/PIO transfer from the given word buffer.
    void start_dma(const uint32_t* data, size_t words);

    /// Abort ongoing DMA (if any).
    void stop_dma();

    uint            _pin, _count;
    bool            _rgbw;
    float           _freq;
    PIO             _pio;
    int             _sm;
    int             _dma_ch;        // -1 if none
    uint            _pio_offset;    // PIO program offset

    std::vector<RGBW>     _buf;       // logical pixel buffer (RGBW)
    std::vector<uint32_t> _frame_tx;  // persistent TX staging buffer (packed words)
    uint8_t         _brightness;      // 0..255
    bool            _gamma_on;
    uint8_t         _gam[256];        // gamma LUT
    bool            _tx_in_flight = false; // DMA active?

    Strip(const Strip&) = delete;
    Strip& operator=(const Strip&) = delete;
};

} // namespace ws
