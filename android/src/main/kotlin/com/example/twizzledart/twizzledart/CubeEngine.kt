package com.example.twizzledart.twizzledart

/**
 * CubeEngine – Kotlin wrapper over the native twizzle-cpp library.
 *
 * Usage:
 *   val engine = CubeEngine()
 *   engine.applyAlgorithm("R U R' U'")
 *   val solved = engine.isSolved()
 *   engine.destroy()
 *
 * Or use with auto-close:
 *   CubeEngine().use { engine ->
 *       engine.applyAlgorithm("T_PERM_ALG")
 *   }
 */
class CubeEngine : AutoCloseable {

    private var handle: Long = 0L

    init {
        handle = nativeCreate()
    }

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    /** Applies a WCA-notation algorithm to the cube state. */
    fun applyAlgorithm(alg: String) {
        checkHandle()
        nativeApplyAlgorithm(handle, alg)
    }

    /** Returns true if the cube is in the solved state. */
    fun isSolved(): Boolean {
        checkHandle()
        return nativeIsSolved(handle)
    }

    /** Resets cube to the solved state. */
    fun reset() {
        checkHandle()
        nativeReset(handle)
    }

    /**
     * Returns the cube state as an IntArray[40]:
     *   [0..7]   corner permutation
     *   [8..15]  corner orientation
     *   [16..27] edge permutation
     *   [28..39] edge orientation
     */
    fun getState(): IntArray {
        checkHandle()
        return nativeGetState(handle)
    }

    /**
     * Parses and re-serialises an algorithm string (useful for canonicalisation).
     * If [inverse] is true, returns the inverse algorithm.
     */
    fun parseAlgorithm(alg: String, inverse: Boolean = false): String {
        checkHandle()
        return nativeParseAlgorithm(handle, alg, inverse)
    }

    /** Frees native memory. Call when done (or use `use { }`). */
    fun destroy() {
        if (handle != 0L) {
            nativeDestroy(handle)
            handle = 0L
        }
    }

    override fun close() = destroy()

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------

    private fun checkHandle() {
        if (handle == 0L) throw IllegalStateException("CubeEngine has been destroyed")
    }

    // -----------------------------------------------------------------------
    // Native declarations
    // -----------------------------------------------------------------------

    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeReset(handle: Long)
    private external fun nativeApplyAlgorithm(handle: Long, alg: String)
    private external fun nativeIsSolved(handle: Long): Boolean
    private external fun nativeGetState(handle: Long): IntArray
    private external fun nativeParseAlgorithm(handle: Long, alg: String, inverse: Boolean): String

    companion object {
        init {
            System.loadLibrary("twizzle")
        }
    }
}
