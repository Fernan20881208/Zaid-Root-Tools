#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <linux/input.h>

namespace zaid {
struct KeyEvent { uint32_t code; bool down; };

// Native getevent equivalent: decoding the Linux evdev binary ABI.
// On Android ARM64 the kernel's struct input_event occupies 24 bytes.
class RawEvdev {
public:
    std::optional<KeyEvent> push(uint8_t byte) {
        m_record[m_used++] = byte;
        if (m_used != m_record.size()) return std::nullopt;
        m_used = 0;
        input_event value{};
        __builtin_memcpy(&value, m_record.data(), sizeof(value));
        if (value.type != EV_KEY || (value.value != 0 && value.value != 1)) return std::nullopt;
        return KeyEvent{value.code, value.value == 1};
    }
private:
    static_assert(sizeof(input_event) == 24, "This release requires Android ARM64 input_event ABI");
    std::array<uint8_t, sizeof(input_event)> m_record{};
    size_t m_used = 0;
};

int mapButtonName(std::string_view name);
}
