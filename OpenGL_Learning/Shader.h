#pragma once
// ============================================================
// Shader.h —— 着色器程序封装
// 负责：编译顶点/片元着色器、链接成 program、传 uniform
// ============================================================

#include <GL/glew.h>
#include <string>
#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

class Shader
{
public:
    GLuint ID = 0;   // OpenGL 程序对象 ID

    Shader() = default;
    Shader(const char* vertexSource, const char* fragmentSource)
    {
        ID = Build(vertexSource, fragmentSource);
    }

    // 禁止拷贝：避免两个对象持有同一个 ID，析构时重复释放
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    ~Shader()
    {
        if (ID) glDeleteProgram(ID);
    }

    // 激活这个着色器程序（之后绘制都走这套 shader）
    void Use() const
    {
        glUseProgram(ID);
    }

    // ---- 下面几个方法：往 shader 里传 uniform ----

    void SetMat4(const char* name, const glm::mat4& value) const
    {
        glUniformMatrix4fv(glGetUniformLocation(ID, name), 1, GL_FALSE, glm::value_ptr(value));
    }

    void SetMat3(const char* name, const glm::mat3& value) const
    {
        glUniformMatrix3fv(glGetUniformLocation(ID, name), 1, GL_FALSE, glm::value_ptr(value));
    }

    void SetVec3(const char* name, const glm::vec3& value) const
    {
        glUniform3fv(glGetUniformLocation(ID, name), 1, glm::value_ptr(value));
    }

    void SetFloat(const char* name, float value) const
    {
        glUniform1f(glGetUniformLocation(ID, name), value);
    }

    void SetInt(const char* name, int value) const
    {
        glUniform1i(glGetUniformLocation(ID, name), value);
    }

    void SetVec3Array(const char* name, const glm::vec3* values, int count) const
    {
        glUniform3fv(glGetUniformLocation(ID, name), count, glm::value_ptr(values[0]));
    }

    void SetFloatArray(const char* name, const float* values, int count) const
    {
        glUniform1fv(glGetUniformLocation(ID, name), count, values);
    }

private:
    // 编译单个着色器对象，失败时打印 GPU 返回的日志
    static GLuint Compile(GLenum type, const char* source)
    {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        GLint success;
        GLchar log[512];
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            glGetShaderInfoLog(shader, 512, nullptr, log);
            std::cerr << "[Shader] 编译失败: " << log << std::endl;
        }
        return shader;
    }

    // 编译两个着色器并链接成完整程序
    static GLuint Build(const char* vertexSource, const char* fragmentSource)
    {
        GLuint vertex   = Compile(GL_VERTEX_SHADER, vertexSource);
        GLuint fragment = Compile(GL_FRAGMENT_SHADER, fragmentSource);

        GLuint program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);

        GLint success;
        GLchar log[512];
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success)
        {
            glGetProgramInfoLog(program, 512, nullptr, log);
            std::cerr << "[Shader] 链接失败: " << log << std::endl;
        }

        glDeleteShader(vertex);      // 链接完成后单独的对象不再需要
        glDeleteShader(fragment);
        return program;
    }
};