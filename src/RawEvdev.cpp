#include "RawEvdev.hpp"
namespace zaid {
int mapButtonName(std::string_view name) {
    if (name == "VOL_UP") return KEY_VOLUMEUP;
    if (name == "VOL_DOWN") return KEY_VOLUMEDOWN;
    if (name == "BTN_SOUTH") return BTN_SOUTH;
    if (name == "BTN_EAST") return BTN_EAST;
    if (name == "BTN_NORTH") return BTN_NORTH;
    if (name == "BTN_WEST") return BTN_WEST;
    if (name == "BTN_TL") return BTN_TL;
    if (name == "BTN_TR") return BTN_TR;
    if (name == "BTN_SELECT") return BTN_SELECT;
    if (name == "BTN_START") return BTN_START;
    return -1;
}
}
