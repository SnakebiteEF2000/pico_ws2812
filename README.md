# WS2812 / SK6812 Driver for Raspberry Pi Pico series (Pico SDK + PIO)

A compact C++ driver for WS2812(B) (RGB) and SK6812 (RGBW) LEDs on the RP2040 and RP2350 using PIO.  
Clean API (setPixel, setAll, show), optional DMA, brightness, and gamma correction.

---

## Features

- PIO-based, precise timing (800 kHz default; 400 kHz supported).
- Optional DMA (enabled by default, falls back to blocking).
- RGB (GRB order) and RGBW (GRBW) support.
- Global brightness (0–255) and ~2.2 gamma correction.
- Blocking (show) and non-blocking (showAsync + busy / wait).

---

## Wiring (do this)

- **DIN** → Pico GPIO via **330–470 Ω** series resistor.
- **5 V** and **GND** from a **separate** 5 V supply sized for your LEDs  
  (rule: up to **60 mA/LED** at full white).
- **Common ground** between Pico, PSU, and strip.
- **≥1000 µF** electrolytic across 5 V/GND at the strip input.
- Some strips need **5 V data** → use **74AHCT125/245** level shifter if needed.

---

## Build / Integration

### Method 1: Using CMake FetchContent (Recommended)

In your project's `CMakeLists.txt`:

```cmake
include(FetchContent)

FetchContent_Declare(
    pico_ws2812
    GIT_REPOSITORY https://github.com/SnakebiteEF2000/pico_ws2812.git
    GIT_TAG main  # or specify a version tag
)
FetchContent_MakeAvailable(pico_ws2812)

add_executable(my_app main.cpp)
target_link_libraries(my_app
    pico_stdlib
    ws2812
)
pico_add_extra_outputs(my_app)
```

### Method 2: As a Subdirectory

If you have the library locally:

```cmake
add_subdirectory(path/to/pico_ws2812)

add_executable(my_app main.cpp)
target_link_libraries(my_app
    pico_stdlib
    ws2812
)
pico_add_extra_outputs(my_app)
```

### Method 3: Manual Integration

Alternatively, you can manually add the driver files (ws2812.cpp, ws2812.hpp, ws2812.pio) to your project:

```cmake
add_library(ws2812
    ws2812.cpp
)
target_include_directories(ws2812 PUBLIC ${CMAKE_CURRENT_LIST_DIR})
pico_generate_pio_header(ws2812 ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)

target_link_libraries(ws2812
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_clocks
)

add_executable(my_app main.cpp)
target_link_libraries(my_app
    pico_stdlib
    ws2812
)
pico_add_extra_outputs(my_app)
```

**Notes**
- pico_generate_pio_header(...) compiles the .pio and generates the C header.
- If your .pio uses `.side_set 1` non-optional, every instruction must specify `side`.  
  Alternatively use `.side_set 1 opt`.

---

## Quickstart
cpp
``` cpp
#include "pico/stdlib.h"
#include "ws2812.hpp"

int main() {
    stdio_init_all();

    // One strip, 30 LEDs, WS2812(B) (RGB), 800 kHz on GPIO16
    ws::Strip strip(/*pin=*/16, /*count=*/30, /*rgbw=*/false, 800000.0f, pio0, -1);
    if (!strip.begin()) { while (true) {} }

    strip.enableGamma(true);   // nicer perception
    strip.setBrightness(128);  // ~50%

    // Clear and send
    strip.clear();
    strip.show();

    // LED 0 blue
    strip.setPixel(0, ws::RGB{0,0,255});
    strip.show();

    // Rainbow sweep
    float h = 0.f;
    while (true) {
        for (uint i = 0; i < strip.size(); ++i)
            strip.setPixel(i, ws::Strip::hsv(h + i*8.f, 1.f, 0.4f));
        strip.show();          // or: strip.showAsync(); strip.wait();
        h += 2.5f;
        sleep_ms(15);
    }
}
```

**RGBW (SK6812) example**

```cpp
ws::Strip strip(16, 8, /*rgbw=*/true);  // GRBW order
strip.begin();
strip.setBrightness(255);
strip.setPixel(0, ws::RGBW{0,0,0,255}); // pure white via W
strip.show();
```

---

## API Reference

```cpp
// Construct (no HW init yet)
Strip(uint pin, uint count, bool rgbw=false, float freq=800000.0f, PIO pio=pio0, int sm=-1);

// Init / teardown
bool begin();
void end();
uint size() const;

// Buffer ops (no I/O until show/showAsync)
void clear();
void setAll(RGB c);
void setAll(RGBW c);
void setPixel(uint i, RGB c);
void setPixel(uint i, uint8_t r, uint8_t g, uint8_t b);
void setPixel(uint i, RGBW c);

// Rendering controls
void setBrightness(uint8_t b); // 0..255
void enableGamma(bool on);

// I/O
void show();       // blocking send + ≥80 µs reset latch
void showAsync();  // start transfer, returns immediately
bool busy() const; // true while transfer in flight
void wait();       // blocks until done + reset latch

// Utilities
static RGB hsv(float h, float s, float v); // H: 0..360, S/V: 0..1
```

**Important**
- set* only updates the internal buffer. Call show()/showAsync() to transmit.
- Valid indices: `0..size()-1`.
- With rgbw=false (WS2812), the w component is ignored.

---

## Timing / Frequency

- Default: **800 kHz**. For picky/legacy strips, try **400 kHz** (pass to constructor).
- The PIO program uses T1/T2/T3; the driver computes the clock divider from clk_sys and cycles/bit.
- Reset latch: **≥80 µs** enforced in show()/wait().

---

## DMA / Async Behavior

- The driver builds a packed frame in a **persistent staging buffer** and starts DMA/PIO.
- show() waits for completion; showAsync() doesn’t.
- Before starting another frame: **call wait()** or poll **busy()**.
- Compile-time switch: WS2812_USE_DMA (default 1). Set to 0 to disable.

---

## Multiple Strips

- Create one ws::Strip per output. You can use pio0 and pio1 and multiple SMs.

```cpp
ws::Strip a(16, 60, false, 800000.0f, pio0, -1);
ws::Strip b(17, 60, false, 800000.0f, pio0, -1);
a.begin(); b.begin();

// Update both buffers...
a.showAsync();
b.show();      // waits for b; optionally a.wait() if needed
```

---

## Common Pitfalls

- **Nothing lights up**
  - Off-by-one index (on count=1, only index **0** is valid).
  - Forgot to call show() after setPixel().
  - Some strips need **5 V data** → use **74AHCT125/245** level shifter.
- **Flicker / random colors**
  - Missing **common ground**.
  - No **series resistor** on data.
  - No **bulk cap** at strip input.
- **pioasm “side required”**
  - With `.side_set 1` (non-optional), every instruction must include `side`.  
    Add missing `side 0` or switch to `.side_set 1 opt`.

---

## Performance Tips

- Keep DMA enabled. Update only the logical buffer between frames.
- Precompute LUTs for effects; prefer integer math in hot paths.
- Use showAsync() and compute the next frame while the current one is sending.
- For very large strips, reduce frame rate or color depth to limit bandwidth.

---

## Power / Safety

- Budget **~60 mA/LED** at full white. Use adequate PSU and wire gauge.
- Inject power at multiple points on long runs to avoid voltage drop and color shift.

---

## License / Credits

- PIO core is based on the official Raspberry Pi examples.
