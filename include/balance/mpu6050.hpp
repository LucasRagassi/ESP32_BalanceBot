#pragma once

#include <cstdint>

namespace balance {

constexpr bool isCompatibleMpu6050Identity(uint8_t whoAmI) {
    return (whoAmI & 0x7eU) == 0x68U;
}

}
