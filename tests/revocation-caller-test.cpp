#include "v2/RevocationCaller.hpp"
#include <initializer_list>
#include <iostream>

int main() {
    using vulkanizer::v2::live::caller_audit::callEndsAt;
    const auto check = [](std::initializer_list<std::uint8_t> bytes, bool expected) {
        return callEndsAt({bytes.begin(), bytes.size()}) == expected;
    };
    if (!check({0xe8, 0, 0, 0, 0}, true) ||
        !check({0x41, 0xff, 0xd0}, true) ||
        !check({0xff, 0x15, 1, 2, 3, 4}, true) ||
        !check({0xff, 0x54, 0x24, 0x08}, true) ||
        !check({0xff, 0x14, 0x25, 1, 2, 3, 4}, true) ||
        !check({0xff, 0x50, 0x08}, true) ||
        !check({0xff, 0xe0}, false) ||
        !check({0xff, 0x15, 1, 2}, false) ||
        !check({0xe8, 0, 0}, false) ||
        !check({0x90, 0x90, 0x90}, false) ||
        !check({}, false)) return 1;
    std::cout << "11 return-PC candidate cases passed\n";
}
