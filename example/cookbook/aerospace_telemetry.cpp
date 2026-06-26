/// @file aerospace_telemetry.cpp
/// @brief Aerospace cookbook: a telemetry frame store.
/// @author Bertin Balouki SIMYELI
///
/// Track smoothly-varying altitude with delta_value, flag changed subsystems per frame with
/// dirty_mask, and report which frames carry a temperature reading via the validity bitmap.

#include <cstdint>

#include "output.hpp"
#include "soatable/soatable.hpp"

namespace {
enum class Subsystem : std::uint32_t {
    avionics   = 1U << 0U,
    propulsion = 1U << 1U,
    thermal    = 1U << 2U,
};

struct Altitude {
    soatable::delta_value<float> metres {0.0F};
};
struct Temperature {
    float celsius = 0.0F;
};
struct Status {
    soatable::dirty_mask<Subsystem> flags;
};
}  // namespace

int main() {
    soatable::soa_table<Altitude, Temperature, Status> frames;

    for (int frame = 0; frame < 5; ++frame) {
        const auto id = frames.insert();

        Altitude altitude;
        altitude.metres.apply_delta(altitude.metres.get_delta(100.0F + static_cast<float>(frame)));
        frames.assign<Altitude>(id, altitude);

        Status status;
        status.flags.mark_dirty(Subsystem::avionics);
        if (frame % 2 == 0) {
            status.flags.mark_dirty(Subsystem::thermal);
            frames.assign<Temperature>(id, Temperature {20.0F + static_cast<float>(frame)});
        }
        frames.assign<Status>(id, status);
    }

    // Report each frame and which subsystems it flagged dirty.
    for (auto [id, altitude, status] : frames.select<Altitude, Status>()) {
        static_cast<void>(id);
        OUT_PRINTLN(
            "altitude {:.1f} m, thermal dirty: {}",
            altitude.get().metres.get(),
            status.get().flags.is_dirty(Subsystem::thermal)
        );
    }

    // Validity bitmap: how many frames carry a temperature reading.
    OUT_PRINTLN("frames with temperature: {}", frames.validity<Temperature>().count());
    return 0;
}
