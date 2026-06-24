// Scientific cookbook: a measurement table. Hand a column straight to a vectorized kernel via the
// zero-copy span, reduce it, and store a compact quantized confidence per sample.

#include <cmath>
#include <cstdint>

#include "soatable/compute.hpp"
#include "soatable/soatable.hpp"

#include "output.hpp"

namespace {
struct Signal {
    double value = 0.0;
};
struct Energy {
    double value = 0.0;
};
// Confidence in [0, 1] stored in 8 bits.
using Confidence = soatable::quantized_float<std::uint8_t, 0, 1000, 8>;
struct Sample {
    Confidence confidence {};
};
}  // namespace

int main() {
    soatable::soa_table<Signal, Energy, Sample> data;

    for (int i = 0; i < 8; ++i) {
        const auto id = data.insert();
        data.assign<Signal>(id, Signal {std::sin(static_cast<double>(i))});
        data.assign<Sample>(id, Sample {Confidence {0.5 + 0.05 * i}});
    }

    // energy = signal^2, computed row-wise, then summed via a zero-copy span reduction.
    soatable::compute::assign_from<Energy, Signal>(
        data, [](const Signal& s) { return Energy {s.value * s.value}; }
    );
    const double total_energy = soatable::compute::reduce_column<Energy>(
        data, 0.0, [](double acc, const Energy& e) { return acc + e.value; }
    );
    OUT_PRINTLN("total energy: {:.4f}", total_energy);

    // Average confidence (dequantized on read).
    double confidence_sum = 0.0;
    for (auto [id, sample] : data.select<Sample>()) {
        static_cast<void>(id);
        confidence_sum += sample.get().confidence.get();
    }
    OUT_PRINTLN("mean confidence: {:.3f}", confidence_sum / static_cast<double>(data.size()));
    return 0;
}
