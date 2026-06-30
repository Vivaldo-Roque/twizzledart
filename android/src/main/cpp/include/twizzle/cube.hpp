#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <stdexcept>

namespace twizzle {

/// One face turn or whole-cube rotation.
enum class Face : uint8_t {
    R = 0, L = 1, U = 2, D = 3, F = 4, B = 5,  ///< Face moves
    M = 6, E = 7, S = 8,                       ///< Slice moves
    X = 9, Y = 10, Z = 11                      ///< Whole cube rotations
};

/// Quarter/half turn direction.
enum class Direction : int8_t {
    CW     =  1,  ///< Clockwise, no suffix (e.g. `R`)
    DOUBLE =  2,  ///< Half turn (e.g. `R2`)
    CCW    = -1   ///< Counter-clockwise, prime (e.g. `R'`)
};

/// A single move: which face/slice/rotation, and how far to turn it.
struct Move {
    Face      face;
    Direction dir;

    bool operator==(const Move& o) const {
        return face == o.face && dir == o.dir;
    }
};

/**
 * @brief Rubik's Cube 3x3x3 state machine.
 *
 * State is stored as corner/edge permutation + orientation arrays
 * (the standard "cubie level" representation used by most solvers).
 *
 * Corner numbering (solved): 0=URF 1=UFL 2=ULB 3=UBR 4=DFR 5=DLF 6=DBL 7=DRB
 * Edge numbering (solved):   0=UR 1=UF 2=UL 3=UB 4=DR 5=DF 6=DL 7=DB
 *                            8=FR 9=FL 10=BL 11=BR
 */
class Cube3x3 {
public:
    static constexpr int NUM_CORNERS = 8;
    static constexpr int NUM_EDGES   = 12;

    /// Raw permutation/orientation state. Safe to copy, serialise, compare.
    struct CubieState {
        std::array<uint8_t, NUM_CORNERS> cp; ///< Corner permutation
        std::array<uint8_t, NUM_CORNERS> co; ///< Corner orientation (0-2)
        std::array<uint8_t, NUM_EDGES>   ep; ///< Edge permutation
        std::array<uint8_t, NUM_EDGES>   eo; ///< Edge orientation (0-1)
    };

    /// Constructs an already-solved cube.
    Cube3x3();

    /// Constructs a cube from an explicit state (e.g. for testing/scrambles).
    explicit Cube3x3(const CubieState& state);

    /// Applies a single move in place.
    void applyMove(const Move& move);

    /// Applies a sequence of moves in order.
    void applyMoves(const std::vector<Move>& moves);

    /// Resets to the solved state.
    void reset();

    /// @return true if every cubie is in its solved position/orientation.
    bool isSolved() const;

    /// @return a copy of the current permutation/orientation state.
    CubieState getState() const;

    /// @return a short human-readable debug string of the state.
    std::string toString() const;

    /// @return the 54-character sticker string (9 facelets x 6 faces).
    std::string toFaceString() const;

private:
    CubieState state_;

    // Single quarter-turn primitives (clockwise, applied once).
    void moveR(); void moveL();
    void moveU(); void moveD();
    void moveF(); void moveB();
    void moveM(); void moveE(); void moveS();
    void moveX(); void moveY(); void moveZ();

    /// Cycles four corner permutation slots a→b→c→d→a.
    void cycleCornersP(int a, int b, int c, int d);
    /// Cycles four corner orientation slots, adding a fixed twist to each.
    void cycleCornersO(int a, int b, int c, int d,
                       uint8_t oa, uint8_t ob, uint8_t oc, uint8_t od);
    /// Cycles four edge permutation slots a→b→c→d→a.
    void cycleEdgesP(int a, int b, int c, int d);
    /// Cycles four edge permutation slots, optionally flipping orientation.
    void cycleEdgesO(int a, int b, int c, int d, bool flip);
};

} // namespace twizzle
