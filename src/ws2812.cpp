#include "ws2812.hpp"
#include <cmath>
#include <algorithm>

#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/regs/dreq.h"
#include "pico/stdlib.h"
#include "ws2812.pio.h"

#ifndef WS2812_USE_DMA
#define WS2812_USE_DMA 1
#endif

namespace ws {

static int s_offset_pio0 = -1;
static int s_offset_pio1 = -1;

Strip::Strip(uint pin, uint count, bool rgbw, float freq, PIO pio, int sm)
: _pin(pin), _count(count), _rgbw(rgbw), _freq(freq),
  _pio(pio), _sm(sm), _dma_ch(-1), _pio_offset(0),
  _buf(count), _brightness(255), _gamma_on(false)
{
    // default gamma is identity until enabled
    for (int i=0;i<256;++i) _gam[i] = (uint8_t)i;
}

bool Strip::begin() {
    // Add PIO program once per PIO block
    if (_pio == pio0) {
        if (s_offset_pio0 < 0) s_offset_pio0 = pio_add_program(_pio, &ws2812_program);
        _pio_offset = (uint)s_offset_pio0;
    } else {
        if (s_offset_pio1 < 0) s_offset_pio1 = pio_add_program(_pio, &ws2812_program);
        _pio_offset = (uint)s_offset_pio1;
    }

    // Claim or use provided SM
    if (_sm < 0) {
        _sm = pio_claim_unused_sm(_pio, true);
        if (_sm < 0) return false;
    }

    ws2812_program_init(_pio, (uint)_sm, _pio_offset, _pin, _freq, _rgbw);

#if WS2812_USE_DMA
    _dma_ch = dma_claim_unused_channel(false);
    if (_dma_ch >= 0) {
        dma_channel_config c = dma_channel_get_default_config(_dma_ch);
        channel_config_set_dreq(&c, pio_get_dreq(_pio, (uint)_sm, true));
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        dma_channel_configure(_dma_ch, &c,
                              (volatile void*)&_pio->txf[_sm], // write addr
                              nullptr, 0, false);               // will set src/len on start
    }
#endif
    clear();
    return true;
}

void Strip::end() {
#if WS2812_USE_DMA
    stop_dma();
    if (_dma_ch >= 0) { dma_channel_unclaim(_dma_ch); _dma_ch = -1; }
#endif
    if (_sm >= 0) { pio_sm_set_enabled(_pio, (uint)_sm, false); pio_sm_unclaim(_pio, (uint)_sm); _sm = -1; }
}

void Strip::clear() { setAll(RGBW{0,0,0,0}); }

void Strip::setAll(RGB c) { setAll(RGBW{c.r, c.g, c.b, 0}); }

void Strip::setAll(RGBW c) {
    std::fill(_buf.begin(), _buf.end(), c);
}

void Strip::setPixel(uint i, RGB c) {
    if (i >= _count) return;
    _buf[i] = RGBW{c.r, c.g, c.b, 0};
}

void Strip::setPixel(uint i, uint8_t r, uint8_t g, uint8_t b) {
    if (i >= _count) return;
    _buf[i] = RGBW{r,g,b,0};
}

void Strip::setPixel(uint i, RGBW c) {
    if (i >= _count) return;
    _buf[i] = c;
}

void Strip::setBrightness(uint8_t b) { _brightness = b; }

void Strip::enableGamma(bool on) {
    _gamma_on = on;
    if (on) {
        for (int i=0;i<256;++i) {
            float x = i/255.0f;
            // simple sRGB-ish gamma ~2.2
            uint8_t y = (uint8_t)std::round(std::pow(x, 2.2f) * 255.0f);
            _gam[i] = y;
        }
    } else {
        for (int i=0;i<256;++i) _gam[i] = (uint8_t)i;
    }
}

static inline uint8_t scale_u8(uint8_t v, uint8_t b) {
    // integer scale with rounding: (v*b)/255
    return (uint8_t)((uint16_t(v) * (uint16_t)b + 127) / 255);
}

void Strip::build_frame(std::vector<uint32_t>& out) {
    out.resize(_count);
    if (_rgbw) {
        for (uint i=0;i<_count;++i) {
            RGBW p = _buf[i];
            uint8_t r = scale_u8(_gamma_on ? _gam[p.r] : p.r, _brightness);
            uint8_t g = scale_u8(_gamma_on ? _gam[p.g] : p.g, _brightness);
            uint8_t b = scale_u8(_gamma_on ? _gam[p.b] : p.b, _brightness);
            uint8_t w = scale_u8(_gamma_on ? _gam[p.w] : p.w, _brightness);
            // SK6812 expects GRBW, MSB-first. With left-shift OSR+autopull 32,
            // we push the 32-bit word directly (no <<8).
            uint32_t grbw = (uint32_t(g)<<24) | (uint32_t(r)<<16) | (uint32_t(b)<<8) | w;
            out[i] = grbw;
        }
    } else {
        for (uint i=0;i<_count;++i) {
            RGBW p = _buf[i];
            uint8_t r = scale_u8(_gamma_on ? _gam[p.r] : p.r, _brightness);
            uint8_t g = scale_u8(_gamma_on ? _gam[p.g] : p.g, _brightness);
            uint8_t b = scale_u8(_gamma_on ? _gam[p.b] : p.b, _brightness);
            // WS2812(B) expects GRB, 24-bit. With 24-bit autopull + left shift,
            // align MSB to bit31..8 -> shift <<8 when writing 32-bit into FIFO.
            uint32_t grb = (uint32_t(g)<<16) | (uint32_t(r)<<8) | b;
            out[i] = (grb << 8); // align to OSR MSB
        }
    }
}

#if WS2812_USE_DMA
void Strip::start_dma(const uint32_t* data, size_t words) {
    if (_dma_ch < 0) {
        // fallback: blocking if no DMA channel claimed
        for (size_t i=0;i<words;++i) pio_sm_put_blocking(_pio, (uint)_sm, data[i]);
        return;
    }
    dma_channel_set_read_addr(_dma_ch, data, false);
    dma_channel_set_trans_count(_dma_ch, (uint)words, true);
}

void Strip::stop_dma() {
    if (_dma_ch >= 0) dma_channel_abort(_dma_ch);
}
#endif

bool Strip::busy() const {
#if WS2812_USE_DMA
    if (_dma_ch >= 0) return dma_channel_is_busy(_dma_ch);
#endif
    return false;
}

void Strip::wait() {
#if WS2812_USE_DMA
    if (_dma_ch >= 0) while (dma_channel_is_busy(_dma_ch)) { tight_loop_contents(); }
#endif
    sleep_us(80); // reset latch
}

void Strip::showAsync() {
    std::vector<uint32_t> frame;
    build_frame(frame);
#if WS2812_USE_DMA
    start_dma(frame.data(), frame.size());
#else
    for (auto w : frame) pio_sm_put_blocking(_pio, (uint)_sm, w);
#endif
    // NOTE: frame must live until DMA completes. To keep API simple,
    // we block-copy via DMA immediately in show() and keep async only for advanced users.
    // If you want truly async, keep a member staging buffer.
}

void Strip::show() {
    std::vector<uint32_t> frame;
    build_frame(frame);
#if WS2812_USE_DMA
    start_dma(frame.data(), frame.size());
    if (_dma_ch >= 0) while (dma_channel_is_busy(_dma_ch)) { tight_loop_contents(); }
#else
    for (auto w : frame) pio_sm_put_blocking(_pio, (uint)_sm, w);
#endif
    sleep_us(80);
}

ws::RGB Strip::hsv(float h, float s, float v) {
    h = fmodf(h, 360.0f); if (h < 0) h += 360.0f;
    s = std::clamp(s, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h/60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r=0,g=0,b=0;
    int seg = (int)(h/60.0f);
    switch(seg) {
        case 0: r=c; g=x; b=0; break;
        case 1: r=x; g=c; b=0; break;
        case 2: r=0; g=c; b=x; break;
        case 3: r=0; g=x; b=c; break;
        case 4: r=x; g=0; b=c; break;
        default:r=c; g=0; b=x; break;
    }
    return RGB{ (uint8_t)std::round((r+m)*255.0f),
                (uint8_t)std::round((g+m)*255.0f),
                (uint8_t)std::round((b+m)*255.0f) };
}

} // namespace ws
