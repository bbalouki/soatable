// Round-trip and bound coverage for the standalone helpers: quantized_float, packed_bits,
// delta_value, and dirty_mask. Sweeps use deterministic value ranges (no randomness).

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "soatable/soatable.hpp"

namespace {

// Default 16-bit quantizer over [0, 1000]; the worst-case round-trip error is one quantization step.
using Quant = soatable::quantized_float<std::uint16_t, 0, 1000000, 16>;
constexpr double quant_min  = 0.0;
constexpr double quant_max  = 1000.0;
constexpr double quant_step = (quant_max - quant_min) / 65535.0;

}  // namespace

TEST(QuantizedFloatTest, RoundTripStaysWithinOneStepAcrossRange) {
    for (int permille = 0; permille <= 1000000; permille += 1000) {
        const double original = permille / 1000.0;
        const Quant  q {original};
        EXPECT_LE(std::abs(q.get() - original), quant_step)
            << "original=" << original;
    }
}

TEST(QuantizedFloatTest, ClampsOutOfRangeInputs) {
    const Quant below {-5.0};
    EXPECT_NEAR(below.get(), quant_min, quant_step);

    const Quant above {5000.0};
    EXPECT_NEAR(above.get(), quant_max, quant_step);
}

TEST(QuantizedFloatTest, AssignmentAndConversionOperators) {
    Quant q;
    q = 250.0;
    EXPECT_NEAR(static_cast<double>(q), 250.0, quant_step);
}

TEST(PackedBitsTest, SetGetRoundTripPerField) {
    using LowNibble  = soatable::packed_bits<std::uint16_t, std::uint8_t, 0, 4>;
    using HighNibble = soatable::packed_bits<std::uint16_t, std::uint8_t, 4, 4>;

    std::uint16_t container = 0;
    LowNibble::set(container, 0xA);
    HighNibble::set(container, 0x5);

    EXPECT_EQ(LowNibble::get(container), 0xA);
    EXPECT_EQ(HighNibble::get(container), 0x5);

    // Overwriting one field must not disturb the other.
    LowNibble::set(container, 0x3);
    EXPECT_EQ(LowNibble::get(container), 0x3);
    EXPECT_EQ(HighNibble::get(container), 0x5);
}

TEST(DeltaValueTest, DeltaRoundTripStaysWithinOneScaleStep) {
    using Delta = soatable::delta_value<double, std::int16_t, 10>;  // Scale = 0.01.
    constexpr double scale = 0.01;

    Delta tracked {100.0};
    for (double target = 100.0; target <= 101.0; target += 0.05) {
        const std::int16_t delta = tracked.get_delta(target);
        tracked.apply_delta(delta);
        EXPECT_LE(std::abs(tracked.get() - target), scale) << "target=" << target;
        tracked.set(target);  // Re-baseline so accumulation does not drift the assertion.
    }
}

TEST(DeltaValueTest, ConstructionAndConversion) {
    soatable::delta_value<double, std::int16_t, 10> tracked {42.0};
    EXPECT_DOUBLE_EQ(static_cast<double>(tracked), 42.0);
}

namespace {
enum class Flags : std::uint32_t {
    position = 1U << 0U,
    velocity = 1U << 1U,
    color    = 1U << 2U,
};
}  // namespace

TEST(DirtyMaskTest, MarkClearAndQueryFlags) {
    soatable::dirty_mask<Flags> mask;
    EXPECT_FALSE(mask.is_any_dirty());

    mask.mark_dirty(Flags::position);
    mask.mark_dirty(Flags::color);
    EXPECT_TRUE(mask.is_dirty(Flags::position));
    EXPECT_TRUE(mask.is_dirty(Flags::color));
    EXPECT_FALSE(mask.is_dirty(Flags::velocity));
    EXPECT_TRUE(mask.is_any_dirty());

    mask.clear_dirty(Flags::position);
    EXPECT_FALSE(mask.is_dirty(Flags::position));
    EXPECT_TRUE(mask.is_dirty(Flags::color));

    mask.reset();
    EXPECT_FALSE(mask.is_any_dirty());
    EXPECT_EQ(mask.get_mask(), 0U);
}
