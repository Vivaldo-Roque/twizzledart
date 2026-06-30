#pragma once

/**
 * twizzle-cpp – Rubik's Cube engine (WCA notation, 3x3x3)
 *
 * Include this single header to get everything:
 *
 *   #include <twizzle/twizzle.hpp>
 *
 *   using namespace twizzle;
 *
 *   Cube3x3   cube;
 *   Algorithm alg("R U R' U'");
 *   alg.execute(cube);
 *   std::cout << cube.toFaceString() << "\n";
 */

#include "cube.hpp"
#include "algorithm.hpp"
