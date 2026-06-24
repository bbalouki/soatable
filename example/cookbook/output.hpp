#pragma once

// Portable println helper shared by the cookbook examples: uses std::println where <print> is
// available (per SOATABLE_HAS_PRINT), otherwise falls back to std::cout + std::format.

#include "soatable/soatable.hpp"

#if SOATABLE_HAS_PRINT
#include <print>
#define OUT_PRINTLN(...) std::println(__VA_ARGS__)
#else
#include <format>
#include <iostream>
#define OUT_PRINTLN(...) std::cout << std::format(__VA_ARGS__) << std::endl
#endif
