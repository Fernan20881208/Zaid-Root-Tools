#include "RawEvdev.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <linux/input.h>

static std::optional<zaid::KeyEvent> push(zaid::RawEvdev& decoder, input_event const& source) {
    std::optional<zaid::KeyEvent> result;
    auto bytes = reinterpret_cast<uint8_t const*>(&source);
    for (size_t i = 0; i < sizeof(source); ++i)
        if (auto value = decoder.push(bytes[i])) result = value;
    return result;
}
int main() {
    zaid::RawEvdev d;
    input_event event{};
    event.type = EV_KEY;
    event.code = KEY_VOLUMEUP;
    event.value = 1;
    auto press = push(d, event);
    assert(press && press->code == KEY_VOLUMEUP && press->down);
    event.value = 2;
    assert(!push(d, event)); // discard autorepeat
    event.value = 0;
    auto up = push(d, event);
    assert(up && !up->down);
    event.type = EV_ABS;
    assert(!push(d, event)); // never parse touch position
    assert(zaid::mapButtonName("VOL_UP") == KEY_VOLUMEUP);
    assert(zaid::mapButtonName("PASSWORD") == -1);
    std::puts("native evdev parser OK");
}
