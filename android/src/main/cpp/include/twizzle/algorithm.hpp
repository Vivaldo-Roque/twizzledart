#pragma once
#include <string>
#include <vector>
#include "cube.hpp"

namespace twizzle {

/**
 * @brief Parses WCA notation algorithm strings into Move sequences.
 *
 * Supported tokens: `R L U D F B` (face), `'` (prime/CCW), `2` (double),
 * `M E S` (slice), `x y z` (cube rotations), `Rw Lw Uw Dw Fw Bw` (wide,
 * treated as face + slice). Whitespace/newlines are ignored; `//` and
 * `(* *)` comments are stripped.
 */
class AlgorithmParser {
public:
    /// @return the parsed Move sequence for a WCA notation string.
    static std::vector<Move> parse(const std::string& algStr);

    /// @return the inverse sequence (reversed order, each move flipped).
    static std::vector<Move> inverse(const std::vector<Move>& moves);

    /// @return a Move sequence rendered back into WCA notation.
    static std::string toString(const std::vector<Move>& moves);
};

/// High-level wrapper around a parsed move sequence.
class Algorithm {
public:
    Algorithm() = default;

    /// Parses `algStr` immediately via AlgorithmParser.
    explicit Algorithm(const std::string& algStr);

    /// Wraps an already-parsed move sequence.
    explicit Algorithm(const std::vector<Move>& moves);

    /// Applies every move to `cube`, in order.
    void execute(Cube3x3& cube) const;

    /// @return a new Algorithm that undoes this one.
    Algorithm inverse() const;

    /// @return a new Algorithm with `other`'s moves appended.
    Algorithm operator+(const Algorithm& other) const;

    /// @return number of moves in the sequence.
    size_t length() const { return moves_.size(); }

    /// @return the sequence rendered back into WCA notation.
    std::string toString() const;

    /// @return read-only access to the underlying moves.
    const std::vector<Move>& moves() const { return moves_; }

private:
    std::vector<Move> moves_;
};

} // namespace twizzle
