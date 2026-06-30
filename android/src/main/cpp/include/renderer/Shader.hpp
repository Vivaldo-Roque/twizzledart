#pragma once
#include "gl_platform.hpp"
#include <string>

namespace twizzle::renderer {

/**
 * @brief Compiles and links a GLSL vertex+fragment program.
 *
 * The platform `#version`/precision preamble (see glslPreamble()) is
 * injected automatically — pass raw shader bodies to build(), without
 * a `#version` line.
 */
class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&)            = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&& o) noexcept;
    Shader& operator=(Shader&& o) noexcept;

    /**
     * @brief Compiles and links a vertex + fragment shader pair.
     * @param vertSrc GLSL vertex shader body (no `#version` line).
     * @param fragSrc GLSL fragment shader body (no `#version` line).
     * @return true on success; failures are logged to stderr.
     */
    bool build(const char* vertSrc, const char* fragSrc);

    /// Activates this program for subsequent draw calls.
    void bind() const;
    /// Deactivates the current program (binds program 0).
    void unbind() const;
    /// @return true if build() succeeded and the program is usable.
    bool valid() const { return id_ != 0; }

    void setInt  (const char* name, int v)              const;
    void setFloat(const char* name, float v)            const;
    void setVec3 (const char* name, const glm::vec3& v) const;
    void setMat3 (const char* name, const glm::mat3& v) const;
    void setMat4 (const char* name, const glm::mat4& v) const;

    /// @return the raw GL program handle (e.g. for manual uniform calls).
    GLuint id() const { return id_; }

private:
    GLuint id_ = 0;

    static GLuint compileStage(GLenum type, const char* preamble, const char* src);
    static bool   checkCompile(GLuint shader);
    static bool   checkLink   (GLuint program);

    int loc(const char* name) const;
};

} // namespace twizzle::renderer
