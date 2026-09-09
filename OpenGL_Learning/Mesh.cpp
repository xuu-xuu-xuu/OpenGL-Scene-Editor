#include "Mesh.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ---------- 顶点上传 ----------

Mesh::Mesh(const std::vector<float>& data, Layout layout)
{
    Upload(data, layout);
}

Mesh::Mesh(Mesh&& other) noexcept
    : m_vao(other.m_vao), m_vbo(other.m_vbo), m_count(other.m_count)
{
    other.m_vao = 0;
    other.m_vbo = 0;
    other.m_count = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept
{
    if (this != &other)
    {
        Release();
        m_vao = other.m_vao;
        m_vbo = other.m_vbo;
        m_count = other.m_count;
        other.m_vao = 0;
        other.m_vbo = 0;
        other.m_count = 0;
    }
    return *this;
}

Mesh::~Mesh()
{
    Release();
}

void Mesh::Release()
{
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    m_vao = 0;
    m_vbo = 0;
    m_count = 0;
}

void Mesh::Upload(const std::vector<float>& data, Layout layout)
{
    int strideFloats = 0;
    switch (layout)
    {
    case Layout::PositionNormal:  strideFloats = 6; break;
    case Layout::PositionColor:   strideFloats = 6; break;
    case Layout::PositionNormalUV:strideFloats = 8; break;
    }
    const GLsizei strideBytes = strideFloats * (GLsizei)sizeof(float);

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(data.size() * sizeof(float)),
                 data.data(), GL_STATIC_DRAW);

    // 位置
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, strideBytes, (void*)0);
    glEnableVertexAttribArray(0);
    // 法线 / 颜色
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, strideBytes,
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    if (layout == Layout::PositionNormalUV)
    {
        // UV
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, strideBytes,
                              (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
    }
    glBindVertexArray(0);

    m_count = (GLsizei)(data.size() / (size_t)strideFloats);
}

void Mesh::Draw(GLenum mode) const
{
    glBindVertexArray(m_vao);
    glDrawArrays(mode, 0, m_count);
    glBindVertexArray(0);
}

// ---------- 几何数据生成 ----------

namespace
{
// 立方体（位置+法线，6 float/顶点）
std::vector<float> MakeCubeData()
{
    std::vector<float> data;
    auto pushQuad = [&](const glm::vec3& n,
                        const glm::vec3& p0, const glm::vec3& p1,
                        const glm::vec3& p2, const glm::vec3& p3)
    {
        auto push = [&](const glm::vec3& p)
        {
            data.push_back(p.x); data.push_back(p.y); data.push_back(p.z);
            data.push_back(n.x); data.push_back(n.y); data.push_back(n.z);
        };
        push(p0); push(p1); push(p3);
        push(p0); push(p3); push(p2);
    };
    for (int axis = 0; axis < 3; ++axis)
        for (int sign = -1; sign <= 1; sign += 2)
        {
            glm::vec3 n(0.0f);
            n[axis] = (float)sign;
            glm::vec3 corners[4];
            int c = 0;
            for (int i = -1; i <= 1; i += 2)
                for (int j = -1; j <= 1; j += 2)
                {
                    glm::vec3 v(0.0f);
                    v[axis] = (float)sign * 0.5f;
                    int a = (axis + 1) % 3;
                    int b = (axis + 2) % 3;
                    v[a] = (float)i * 0.5f;
                    v[b] = (float)j * 0.5f;
                    corners[c++] = v;
                }
            pushQuad(n, corners[0], corners[1], corners[2], corners[3]);
        }
    return data;
}

// 球体（仅位置，3 float/顶点）
// 注：与原实现保持一致——球体数据只含位置，仍按 6 float 步长上传，
//     只被 flatShader（仅读位置）使用。
std::vector<float> MakeSphereData(int segments, int rings)
{
    const float PI = 3.14159265f;
    std::vector<float> data;
    auto push = [&](const glm::vec3& p)
    {
        data.push_back(p.x); data.push_back(p.y); data.push_back(p.z);
    };
    for (int j = 0; j < rings; ++j)
    {
        float v0 = (float)j / rings * PI;
        float v1 = (float)(j + 1) / rings * PI;
        for (int i = 0; i < segments; ++i)
        {
            float u0 = (float)i / segments * 2.0f * PI;
            float u1 = (float)((i + 1) % segments) / segments * 2.0f * PI;
            glm::vec3 a(sinf(v0) * cosf(u0), cosf(v0), sinf(v0) * sinf(u0));
            glm::vec3 b(sinf(v0) * cosf(u1), cosf(v0), sinf(v0) * sinf(u1));
            glm::vec3 c(sinf(v1) * cosf(u0), cosf(v1), sinf(v1) * sinf(u0));
            glm::vec3 d(sinf(v1) * cosf(u1), cosf(v1), sinf(v1) * sinf(u1));
            push(a); push(d); push(b);
            push(a); push(c); push(d);
        }
    }
    return data;
}

// 网格（位置+颜色，6 float/顶点）
std::vector<float> MakeGridData()
{
    std::vector<float> data;
    auto line = [&](float x0, float y0, float z0,
                    float x1, float y1, float z1,
                    float r, float g, float b)
    {
        data.push_back(x0); data.push_back(y0); data.push_back(z0);
        data.push_back(r);  data.push_back(g);  data.push_back(b);
        data.push_back(x1); data.push_back(y1); data.push_back(z1);
        data.push_back(r);  data.push_back(g);  data.push_back(b);
    };
    const float half = 12.0f;
    for (int i = -12; i <= 12; ++i)
    {
        bool major = (i % 5 == 0);
        float c = major ? 0.55f : 0.28f;
        float x = (float)i;
        line(x, 0.0f, -half, x, 0.0f, half, c, c, c);
        line(-half, 0.0f, x, half, 0.0f, x, c, c, c);
    }
    line(0, 0, 0, 3, 0, 0, 1.0f, 0.2f, 0.2f);
    line(0, 0, 0, 0, 3, 0, 0.2f, 1.0f, 0.2f);
    line(0, 0, 0, 0, 0, 3, 0.2f, 0.3f, 1.0f);
    return data;
}

// 坐标轴（位置+颜色，6 float/顶点）
std::vector<float> MakeAxesData()
{
    std::vector<float> data;
    auto line = [&](float x0, float y0, float z0,
                    float x1, float y1, float z1,
                    float r, float g, float b)
    {
        data.push_back(x0); data.push_back(y0); data.push_back(z0);
        data.push_back(r);  data.push_back(g);  data.push_back(b);
        data.push_back(x1); data.push_back(y1); data.push_back(z1);
        data.push_back(r);  data.push_back(g);  data.push_back(b);
    };
    line(0, 0, 0, 1, 0, 0, 1.0f, 0.25f, 0.25f);
    line(0, 0, 0, 0, 1, 0, 0.25f, 1.0f, 0.25f);
    line(0, 0, 0, 0, 0, 1, 0.30f, 0.45f, 1.0f);
    return data;
}

// 灯头箭头（局部指向 -Y，位置+颜色）
std::vector<float> MakeArrowData()
{
    std::vector<float> data;
    auto line = [&](float x0, float y0, float z0, float x1, float y1, float z1)
    {
        for (int k = 0; k < 2; ++k)
        {
            data.push_back(k == 0 ? x0 : x1);
            data.push_back(k == 0 ? y0 : y1);
            data.push_back(k == 0 ? z0 : z1);
            data.push_back(1.0f); data.push_back(0.9f); data.push_back(0.4f);
        }
    };
    line(0, 0, 0, 0, -1.0f, 0);
    line(0, -1.0f, 0, -0.14f, -0.72f, 0);
    line(0, -1.0f, 0,  0.14f, -0.72f, 0);
    return data;
}

// 聚光灯外锥线框：apex 在原点，指向 -Y，底圆半径 1（距离 1 处）
std::vector<float> MakeSpotConeData()
{
    std::vector<float> data;
    auto add = [&](float x0, float y0, float z0, float x1, float y1, float z1)
    {
        for (int k = 0; k < 2; ++k)
        {
            data.push_back(k == 0 ? x0 : x1);
            data.push_back(k == 0 ? y0 : y1);
            data.push_back(k == 0 ? z0 : z1);
            data.push_back(0.55f); data.push_back(0.85f); data.push_back(1.0f);
        }
    };
    const int seg = 10;
    for (int i = 0; i < seg; ++i)
    {
        float a0 = (float)i / seg * 2.0f * 3.14159265f;
        float a1 = (float)((i + 1) % seg) / seg * 2.0f * 3.14159265f;
        float cx0 = cosf(a0), cz0 = sinf(a0);
        float cx1 = cosf(a1), cz1 = sinf(a1);
        add(0, 0, 0, cx0, -1.0f, cz0);          // 伞骨
        add(cx0, -1.0f, cz0, cx1, -1.0f, cz1);  // 底圈
    }
    return data;
}

// 包围盒线框（位置+颜色）
std::vector<float> MakeBoxEdgesData()
{
    std::vector<float> data;
    auto line = [&](float x0, float y0, float z0, float x1, float y1, float z1)
    {
        data.push_back(x0); data.push_back(y0); data.push_back(z0);
        data.push_back(1.0f); data.push_back(0.55f); data.push_back(0.1f);
        data.push_back(x1); data.push_back(y1); data.push_back(z1);
        data.push_back(1.0f); data.push_back(0.55f); data.push_back(0.1f);
    };
    const float h = 0.5f;
    for (int i = -1; i <= 1; i += 2)
    {
        for (int j = -1; j <= 1; j += 2)
        {
            float x = i * h, y = j * h;
            line(x, y, -h, x, y, h);
            line(x, -h, y, x, h, y);
            line(-h, x, y, h, x, y);
        }
    }
    return data;
}
} // namespace

// ---------- 工厂 ----------

Mesh Mesh::Cube()
{
    return Mesh(MakeCubeData(), Layout::PositionNormal);
}

Mesh Mesh::Sphere(int segments, int rings)
{
    // 与原实现一致：球体只有位置数据，仍走 6 float 步长的上传路径。
    Mesh m;
    m.Upload(MakeSphereData(segments, rings), Layout::PositionNormal);
    return m;
}

Mesh Mesh::Grid()
{
    return Mesh(MakeGridData(), Layout::PositionColor);
}

Mesh Mesh::Axes()
{
    return Mesh(MakeAxesData(), Layout::PositionColor);
}

Mesh Mesh::Arrow()
{
    return Mesh(MakeArrowData(), Layout::PositionColor);
}

Mesh Mesh::SpotCone()
{
    return Mesh(MakeSpotConeData(), Layout::PositionColor);
}

Mesh Mesh::BoxEdges()
{
    return Mesh(MakeBoxEdgesData(), Layout::PositionColor);
}
