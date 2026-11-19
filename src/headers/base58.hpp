#pragma once
#include <cstddef>
#include <span>
#include <string>

std::string base58_encode(std::span<const std::byte> input);
