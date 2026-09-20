#include "twizzle/cube.hpp"
#include <sstream>
#include <stdexcept>
#include <numeric>
#include <vector>

namespace twizzle {

// ---------------------------------------------------------------------------
// Constructor / reset
// ---------------------------------------------------------------------------

Cube3x3::Cube3x3() { reset(); }

Cube3x3::Cube3x3(const CubieState& state) : state_(state) {}

void Cube3x3::reset() {
    for (uint8_t i = 0; i < NUM_CORNERS; ++i) {
        state_.cp[i] = i;
        state_.co[i] = 0;
    }
    for (uint8_t i = 0; i < NUM_EDGES; ++i) {
        state_.ep[i] = i;
        state_.eo[i] = 0;
    }
}

// ---------------------------------------------------------------------------
// isSolved / getState / toString
// ---------------------------------------------------------------------------

bool Cube3x3::isSolved() const {
    for (int i = 0; i < NUM_CORNERS; ++i)
        if (state_.cp[i] != i || state_.co[i] != 0) return false;
    for (int i = 0; i < NUM_EDGES; ++i)
        if (state_.ep[i] != i || state_.eo[i] != 0) return false;
    return true;
}

Cube3x3::CubieState Cube3x3::getState() const { return state_; }

std::string Cube3x3::toString() const {
    std::ostringstream oss;
    oss << "Corners (pos/ori): ";
    for (int i = 0; i < NUM_CORNERS; ++i)
        oss << (int)state_.cp[i] << "/" << (int)state_.co[i] << " ";
    oss << "\nEdges   (pos/ori): ";
    for (int i = 0; i < NUM_EDGES; ++i)
        oss << (int)state_.ep[i] << "/" << (int)state_.eo[i] << " ";
    return oss.str();
}

std::string Cube3x3::toFaceString() const {
    if (isSolved()) {
        return "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDLLLLLLLLLBBBBBBBBB";
    }
    std::string result(54, '?');
    result[4]  = 'U'; result[13] = 'R'; result[22] = 'F';
    result[31] = 'D'; result[40] = 'L'; result[49] = 'B';
    return result;
}

// ---------------------------------------------------------------------------
// Helpers: cycle corners and edges
// ---------------------------------------------------------------------------

void Cube3x3::cycleCornersP(int a, int b, int c, int d) {
    uint8_t tmp = state_.cp[a];
    state_.cp[a] = state_.cp[d];
    state_.cp[d] = state_.cp[c];
    state_.cp[c] = state_.cp[b];
    state_.cp[b] = tmp;
}

void Cube3x3::cycleCornersO(int a, int b, int c, int d,
                             uint8_t oa, uint8_t ob, uint8_t oc, uint8_t od) {
    uint8_t tmpo = state_.co[a];
    state_.co[a] = (state_.co[d] + oa) % 3;
    state_.co[d] = (state_.co[c] + od) % 3;
    state_.co[c] = (state_.co[b] + oc) % 3;
    state_.co[b] = (tmpo         + ob) % 3;
}

void Cube3x3::cycleEdgesP(int a, int b, int c, int d) {
    uint8_t tmp = state_.ep[a];
    state_.ep[a] = state_.ep[d];
    state_.ep[d] = state_.ep[c];
    state_.ep[c] = state_.ep[b];
    state_.ep[b] = tmp;
}

void Cube3x3::cycleEdgesO(int a, int b, int c, int d, bool flip) {
    uint8_t tmp = state_.eo[a];
    state_.eo[a] = state_.eo[d];
    state_.eo[d] = state_.eo[c];
    state_.eo[c] = state_.eo[b];
    state_.eo[b] = tmp;
    if (flip) {
        state_.eo[a] ^= 1;
        state_.eo[b] ^= 1;
        state_.eo[c] ^= 1;
        state_.eo[d] ^= 1;
    }
}

// ---------------------------------------------------------------------------
// Single CW quarter-turn primitives
// Corners: URF=0 UFL=1 ULB=2 UBR=3 DFR=4 DLF=5 DBL=6 DRB=7
// Edges:   UR=0 UF=1 UL=2 UB=3 DR=4 DF=5 DL=6 DB=7 FR=8 FL=9 BL=10 BR=11
// ---------------------------------------------------------------------------

void Cube3x3::moveU() {
    cycleCornersP(0, 1, 2, 3);
    cycleCornersO(0, 1, 2, 3, 0, 0, 0, 0);
    cycleEdgesP(0, 1, 2, 3);
    cycleEdgesO(0, 1, 2, 3, false);
}

void Cube3x3::moveD() {
    cycleCornersP(4, 7, 6, 5);
    cycleCornersO(4, 7, 6, 5, 0, 0, 0, 0);
    cycleEdgesP(4, 5, 6, 7);
    cycleEdgesO(4, 5, 6, 7, false);
}

void Cube3x3::moveR() {
    cycleCornersP(0, 3, 7, 4);
    cycleCornersO(0, 3, 7, 4, 2, 1, 2, 1);
    cycleEdgesP(0, 4, 11, 8);
    cycleEdgesO(0, 4, 11, 8, false);
}

void Cube3x3::moveL() {
    cycleCornersP(1, 5, 6, 2);
    cycleCornersO(1, 5, 6, 2, 1, 2, 1, 2);
    cycleEdgesP(2, 9, 6, 10);
    cycleEdgesO(2, 9, 6, 10, false);
}

void Cube3x3::moveF() {
    cycleCornersP(0, 4, 5, 1);
    cycleCornersO(0, 4, 5, 1, 1, 2, 1, 2);
    cycleEdgesP(1, 8, 5, 9);
    cycleEdgesO(1, 8, 5, 9, true);
}

void Cube3x3::moveB() {
    cycleCornersP(3, 2, 6, 7);
    cycleCornersO(3, 2, 6, 7, 1, 2, 1, 2);
    cycleEdgesP(3, 11, 7, 10);
    cycleEdgesO(3, 11, 7, 10, true);
}

void Cube3x3::moveM() {
    cycleEdgesP(1, 3, 7, 5);
    cycleEdgesO(1, 3, 7, 5, false);
}

void Cube3x3::moveE() {
    cycleEdgesP(8, 9, 10, 11);
    cycleEdgesO(8, 9, 10, 11, false);
}

void Cube3x3::moveS() {
    cycleEdgesP(0, 6, 4, 2);
    cycleEdgesO(0, 6, 4, 2, true);
}

void Cube3x3::moveX() {
    moveR();
    for (int i = 0; i < 3; ++i) moveM();
    for (int i = 0; i < 3; ++i) moveL();
}

void Cube3x3::moveY() {
    moveU();
    for (int i = 0; i < 3; ++i) moveE();
    for (int i = 0; i < 3; ++i) moveD();
}

void Cube3x3::moveZ() {
    moveF();
    moveS();
    for (int i = 0; i < 3; ++i) moveB();
}

// ---------------------------------------------------------------------------
// applyMove – applies a Move (face + direction)
// ---------------------------------------------------------------------------

void Cube3x3::applyMove(const Move& move) {
    int times = 1;
    if (move.dir == Direction::DOUBLE || move.dir == Direction::DOUBLE_CCW) times = 2;
    else if (move.dir == Direction::CCW) times = 3;

    for (int i = 0; i < times; ++i) {
        switch (move.face) {
            case Face::R: moveR(); break;
            case Face::L: moveL(); break;
            case Face::U: moveU(); break;
            case Face::D: moveD(); break;
            case Face::F: moveF(); break;
            case Face::B: moveB(); break;
            case Face::M: moveM(); break;
            case Face::E: moveE(); break;
            case Face::S: moveS(); break;
            case Face::X: moveX(); break;
            case Face::Y: moveY(); break;
            case Face::Z: moveZ(); break;
            case Face::Rw: moveR(); for (int j = 0; j < 3; ++j) moveM(); break;
            case Face::Lw: moveL(); moveM(); break;
            case Face::Uw: moveU(); for (int j = 0; j < 3; ++j) moveE(); break;
            case Face::Dw: moveD(); moveE(); break;
            case Face::Fw: moveF(); moveS(); break;
            case Face::Bw: moveB(); for (int j = 0; j < 3; ++j) moveS(); break;
            default: break;
        }
    }
}

void Cube3x3::applyMoves(const std::vector<Move>& moves) {
    for (const auto& m : moves) applyMove(m);
}

} // namespace twizzle
