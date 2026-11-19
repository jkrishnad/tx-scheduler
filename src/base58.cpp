#include "headers/base58.hpp"
#include <cstddef>
#include <vector>

std::string base58_encode(std::span<const std::byte> input) {
    std::vector<int> digits;
    std::size_t zeros = 0;
    while (zeros < input.size() && input[zeros] == std::byte(0)) {
        zeros++;
    }

    for (std::size_t i = zeros; i < input.size(); i++) {
        int carry = std::to_integer<int>(input[i]);
        for (int &d : digits) {
            carry += d * 256;
            d = carry % 58;
            carry /= 58;
        }
        while (carry > 0) {
            digits.push_back(carry % 58);
            carry /= 58;
        }
    }

    const char *alphabet =
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

    // Leading zero bytes become '1's, then the digits highest first.
    std::string result(zeros, '1');
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        result.push_back(alphabet[*it]);
    }
    return result;
}
