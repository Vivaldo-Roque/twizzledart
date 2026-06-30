#include "twizzle/algorithm.hpp"
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <regex>

namespace twizzle {

// ---------------------------------------------------------------------------
// AlgorithmParser
// ---------------------------------------------------------------------------

/**
 * Parses a WCA notation string into a vector of Move structs.
 *
 * Supported formats:
 *   R   R'  R2  R2'
 *   U   U'  U2
 *   Rw  Rw' Rw2   (wide moves → face + slice)
 *   M   E   S     (slice)
 *   x   y   z     (rotations)
 *   Parentheses and brackets are ignored (used in commutators/conjugates)
 *   Comments //... and (*...*) are stripped
 */
std::vector<Move> AlgorithmParser::parse(const std::string& algStr) {
    std::vector<Move> moves;

    // Strip comments: // to end of line
    std::string cleaned;
    {
        bool inComment = false;
        for (size_t i = 0; i < algStr.size(); ++i) {
            if (!inComment && i + 1 < algStr.size() &&
                algStr[i] == '/' && algStr[i+1] == '/') {
                inComment = true;
            }
            if (algStr[i] == '\n') inComment = false;
            if (!inComment) cleaned += algStr[i];
        }
    }

    // Strip (* ... *) block comments
    {
        std::string tmp;
        bool inBlock = false;
        for (size_t i = 0; i < cleaned.size(); ++i) {
            if (!inBlock && i + 1 < cleaned.size() &&
                cleaned[i] == '(' && cleaned[i+1] == '*') {
                inBlock = true; ++i; continue;
            }
            if (inBlock && i + 1 < cleaned.size() &&
                cleaned[i] == '*' && cleaned[i+1] == ')') {
                inBlock = false; ++i; continue;
            }
            if (!inBlock) tmp += cleaned[i];
        }
        cleaned = tmp;
    }

    // Replace structural characters with spaces
    for (char& c : cleaned) {
        if (c == '(' || c == ')' || c == '[' || c == ']' ||
            c == ',' || c == ':' || c == '\n' || c == '\t') {
            c = ' ';
        }
    }

    std::istringstream iss(cleaned);
    std::string token;

    while (iss >> token) {
        if (token.empty()) continue;

        size_t idx = 0;
        char faceChar = token[idx++];

        // Determine face
        Face face;
        bool wideMove = false;

        switch (std::toupper(faceChar)) {
            case 'R': face = Face::R; break;
            case 'L': face = Face::L; break;
            case 'U': face = Face::U; break;
            case 'D': face = Face::D; break;
            case 'F': face = Face::F; break;
            case 'B': face = Face::B; break;
            case 'M': face = Face::M; break;
            case 'E': face = Face::E; break;
            case 'S': face = Face::S; break;
            case 'X': face = Face::X; break;
            case 'Y': face = Face::Y; break;
            case 'Z': face = Face::Z; break;
            default:
                // Lowercase x y z
                if (faceChar == 'x') { face = Face::X; }
                else if (faceChar == 'y') { face = Face::Y; }
                else if (faceChar == 'z') { face = Face::Z; }
                else { continue; } // Unknown token, skip
        }

        // Check for 'w' (wide move)
        if (idx < token.size() && std::tolower(token[idx]) == 'w') {
            wideMove = true;
            ++idx;
        }

        // Parse modifier: 2, ', 2'
        Direction dir = Direction::CW;
        int  numTurns  = 1;

        while (idx < token.size()) {
            char c = token[idx];
            if (c == '2') {
                numTurns = 2;
                ++idx;
            } else if (c == '\'') {
                dir = (numTurns == 2) ? Direction::DOUBLE : Direction::CCW;
                ++idx;
            } else {
                ++idx; // unexpected, skip
            }
        }

        if (numTurns == 2 && dir == Direction::CW) {
            dir = Direction::DOUBLE;
        }

        // Wide move: emit face move + corresponding slice
        if (wideMove) {
            moves.push_back({face, dir});
            Face slice;
            Direction sliceDir = dir;
            switch (face) {
                case Face::R: slice = Face::M;
                    // Rw = R + M'  → slice is opposite
                    sliceDir = (dir == Direction::CW)  ? Direction::CCW  :
                               (dir == Direction::CCW) ? Direction::CW   :
                                                         Direction::DOUBLE;
                    break;
                case Face::L: slice = Face::M;    break;
                case Face::U: slice = Face::E;
                    sliceDir = (dir == Direction::CW)  ? Direction::CCW  :
                               (dir == Direction::CCW) ? Direction::CW   :
                                                         Direction::DOUBLE;
                    break;
                case Face::D: slice = Face::E;    break;
                case Face::F: slice = Face::S;    break;
                case Face::B: slice = Face::S;
                    sliceDir = (dir == Direction::CW)  ? Direction::CCW  :
                               (dir == Direction::CCW) ? Direction::CW   :
                                                         Direction::DOUBLE;
                    break;
                default:      slice = face;       break;
            }
            moves.push_back({slice, sliceDir});
        } else {
            moves.push_back({face, dir});
        }
    }

    return moves;
}

// ---------------------------------------------------------------------------
// inverse
// ---------------------------------------------------------------------------

std::vector<Move> AlgorithmParser::inverse(const std::vector<Move>& moves) {
    std::vector<Move> inv;
    inv.reserve(moves.size());
    for (auto it = moves.rbegin(); it != moves.rend(); ++it) {
        Direction newDir = Direction::CW;
        switch (it->dir) {
            case Direction::CW:     newDir = Direction::CCW;    break;
            case Direction::CCW:    newDir = Direction::CW;     break;
            case Direction::DOUBLE: newDir = Direction::DOUBLE; break;
        }
        inv.push_back({it->face, newDir});
    }
    return inv;
}

// ---------------------------------------------------------------------------
// toString
// ---------------------------------------------------------------------------

std::string AlgorithmParser::toString(const std::vector<Move>& moves) {
    std::ostringstream oss;
    for (size_t i = 0; i < moves.size(); ++i) {
        if (i > 0) oss << ' ';
        const auto& m = moves[i];
        switch (m.face) {
            case Face::R: oss << 'R'; break;
            case Face::L: oss << 'L'; break;
            case Face::U: oss << 'U'; break;
            case Face::D: oss << 'D'; break;
            case Face::F: oss << 'F'; break;
            case Face::B: oss << 'B'; break;
            case Face::M: oss << 'M'; break;
            case Face::E: oss << 'E'; break;
            case Face::S: oss << 'S'; break;
            case Face::X: oss << 'x'; break;
            case Face::Y: oss << 'y'; break;
            case Face::Z: oss << 'z'; break;
        }
        switch (m.dir) {
            case Direction::CW:     break;
            case Direction::CCW:    oss << '\''; break;
            case Direction::DOUBLE: oss << '2';  break;
        }
    }
    return oss.str();
}

// ---------------------------------------------------------------------------
// Algorithm
// ---------------------------------------------------------------------------

Algorithm::Algorithm(const std::string& algStr)
    : moves_(AlgorithmParser::parse(algStr)) {}

Algorithm::Algorithm(const std::vector<Move>& moves)
    : moves_(moves) {}

void Algorithm::execute(Cube3x3& cube) const {
    cube.applyMoves(moves_);
}

Algorithm Algorithm::inverse() const {
    return Algorithm(AlgorithmParser::inverse(moves_));
}

Algorithm Algorithm::operator+(const Algorithm& other) const {
    std::vector<Move> combined = moves_;
    combined.insert(combined.end(), other.moves_.begin(), other.moves_.end());
    return Algorithm(combined);
}

std::string Algorithm::toString() const {
    return AlgorithmParser::toString(moves_);
}

} // namespace twizzle
