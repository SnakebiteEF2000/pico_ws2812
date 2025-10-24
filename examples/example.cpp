#include "pico/stdlib.h"
#include "ws2812.hpp"

int main() {
    stdio_init_all();

    // One strip, 30 LEDs, WS2812(B) (RGB), 800 kHz on GPIO16
    ws::Strip strip(/*pin=*/16, /*count=*/30, /*rgbw=*/false, 800000.0f, pio0, -1);
    if (!strip.begin()) { 
        while (true) {} 
    }

    strip.enableGamma(true);   // nicer perception
    strip.setBrightness(128);  // ~50%

    // Rainbow sweep
    float h = 0.f;
    while (true) {
        for (uint i = 0; i < strip.size(); ++i) {
            strip.setPixel(i, ws::Strip::hsv(h + i*8.f, 1.f, 0.4f));
        }
        strip.show();
        h += 2.5f;
        sleep_ms(15);
    }

    return 0;
}