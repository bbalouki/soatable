/// @file units.hpp
/// @brief Opt-in compile-time dimensional analysis for engineering columns.
/// @author Bertin Balouki SIMYELI
///
/// A quantity<T, Dimension> carries its units in the type: adding or subtracting requires matching
/// dimensions (a mismatch is a compile error), while multiplication and division combine
/// dimensions. Use a quantity as a column type to get dimensional safety at table operation
/// boundaries (e.g. you cannot add metres to seconds).
#pragma once

#include <type_traits>

namespace soatable::units {

/// @brief A physical dimension as integer exponents of the base units (length, time, mass).
/// @tparam Length The length exponent.
/// @tparam Time The time exponent.
/// @tparam Mass The mass exponent.
template <int Length, int Time, int Mass>
struct dimension {
    static constexpr int length = Length;
    static constexpr int time   = Time;
    static constexpr int mass   = Mass;
};

/// @brief Sum of two dimensions (used by multiplication).
template <typename A, typename B>
using dimension_product = dimension<A::length + B::length, A::time + B::time, A::mass + B::mass>;

/// @brief Difference of two dimensions (used by division).
template <typename A, typename B>
using dimension_quotient = dimension<A::length - B::length, A::time - B::time, A::mass - B::mass>;

/// @brief A scalar value tagged with a physical dimension.
/// @tparam T The underlying numeric type.
/// @tparam Dimension The dimension (defaults to dimensionless).
template <typename T, typename Dimension = dimension<0, 0, 0>>
class quantity {
   public:
    /// @brief The underlying numeric type.
    using value_type = T;
    /// @brief The dimension tag.
    using dimension_type = Dimension;

    constexpr quantity() = default;
    /// @brief Construct from a raw magnitude in the dimension's units.
    constexpr explicit quantity(T value) : m_value(value) {}

    /// @brief The raw magnitude.
    [[nodiscard]] constexpr T value() const noexcept { return m_value; }

    /// @brief Add two quantities of the same dimension.
    [[nodiscard]] constexpr quantity operator+(const quantity& other) const {
        return quantity(m_value + other.m_value);
    }
    /// @brief Subtract two quantities of the same dimension.
    [[nodiscard]] constexpr quantity operator-(const quantity& other) const {
        return quantity(m_value - other.m_value);
    }

    /// @brief Scale by a dimensionless factor.
    [[nodiscard]] constexpr quantity operator*(T scalar) const {
        return quantity(m_value * scalar);
    }

    /// @brief Multiply by another quantity, adding their dimensions.
    template <typename OtherDimension>
    [[nodiscard]] constexpr quantity<T, dimension_product<Dimension, OtherDimension>> operator*(
        const quantity<T, OtherDimension>& other
    ) const {
        return quantity<T, dimension_product<Dimension, OtherDimension>>(m_value * other.value());
    }

    /// @brief Divide by another quantity, subtracting their dimensions.
    template <typename OtherDimension>
    [[nodiscard]] constexpr quantity<T, dimension_quotient<Dimension, OtherDimension>> operator/(
        const quantity<T, OtherDimension>& other
    ) const {
        return quantity<T, dimension_quotient<Dimension, OtherDimension>>(m_value / other.value());
    }

    /// @brief Compare two quantities of the same dimension.
    [[nodiscard]] constexpr bool operator==(const quantity& other) const = default;

   private:
    T m_value {};
};

// Common dimension tags.
using scalar_dimension       = dimension<0, 0, 0>;
using length_dimension       = dimension<1, 0, 0>;
using time_dimension         = dimension<0, 1, 0>;
using mass_dimension         = dimension<0, 0, 1>;
using velocity_dimension     = dimension<1, -1, 0>;
using acceleration_dimension = dimension<1, -2, 0>;
using force_dimension        = dimension<1, -2, 1>;
using area_dimension         = dimension<2, 0, 0>;

// Convenience quantity aliases (default to double magnitudes).
template <typename T = double>
using length = quantity<T, length_dimension>;
template <typename T = double>
using duration = quantity<T, time_dimension>;
template <typename T = double>
using mass = quantity<T, mass_dimension>;
template <typename T = double>
using velocity = quantity<T, velocity_dimension>;
template <typename T = double>
using acceleration = quantity<T, acceleration_dimension>;
template <typename T = double>
using force = quantity<T, force_dimension>;

}  // namespace soatable::units
