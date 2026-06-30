#include "renderer/Shader.hpp"
#include <cstdio>
#include <cstring>

namespace twizzle::renderer {

// ---------------------------------------------------------------------------
Shader::~Shader() { if (id_) glDeleteProgram(id_); }

Shader::Shader(Shader&& o) noexcept : id_(o.id_) { o.id_ = 0; }
Shader& Shader::operator=(Shader&& o) noexcept {
    if (this != &o) { if (id_) glDeleteProgram(id_); id_ = o.id_; o.id_ = 0; }
    return *this;
}

// ---------------------------------------------------------------------------
GLuint Shader::compileStage(GLenum type, const char* preamble, const char* src) {
    GLuint sh = glCreateShader(type);
    const char* srcs[2] = { preamble, src };
    glShaderSource(sh, 2, srcs, nullptr);
    glCompileShader(sh);
    if (!checkCompile(sh)) { glDeleteShader(sh); return 0; }
    return sh;
}

bool Shader::checkCompile(GLuint sh) {
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(sh, 512, nullptr, log);
        std::fprintf(stderr, "[Shader] compile error:\n%s\n", log);
    }
    return ok == GL_TRUE;
}

bool Shader::checkLink(GLuint prog) {
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetProgramInfoLog(prog, 512, nullptr, log);
        std::fprintf(stderr, "[Shader] link error:\n%s\n", log);
    }
    return ok == GL_TRUE;
}

bool Shader::build(const char* vertSrc, const char* fragSrc) {
    const char* pre = glslPreamble();
    GLuint vs = compileStage(GL_VERTEX_SHADER,   pre, vertSrc); if (!vs) return false;
    GLuint fs = compileStage(GL_FRAGMENT_SHADER,  pre, fragSrc); if (!fs) { glDeleteShader(vs); return false; }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!checkLink(prog)) { glDeleteProgram(prog); return false; }
    if (id_) glDeleteProgram(id_);
    id_ = prog;
    return true;
}

void Shader::bind()   const { glUseProgram(id_); }
void Shader::unbind() const { glUseProgram(0);   }

int Shader::loc(const char* name) const {
    return glGetUniformLocation(id_, name);
}

void Shader::setInt  (const char* n, int v)              const { glUniform1i (loc(n), v);                    }
void Shader::setFloat(const char* n, float v)            const { glUniform1f (loc(n), v);                    }
void Shader::setVec3 (const char* n, const glm::vec3& v) const { glUniform3fv(loc(n), 1, glm::value_ptr(v)); }
void Shader::setMat3 (const char* n, const glm::mat3& m) const { glUniformMatrix3fv(loc(n), 1, GL_FALSE, glm::value_ptr(m)); }
void Shader::setMat4 (const char* n, const glm::mat4& m) const { glUniformMatrix4fv(loc(n), 1, GL_FALSE, glm::value_ptr(m)); }

} // namespace twizzle::renderer
