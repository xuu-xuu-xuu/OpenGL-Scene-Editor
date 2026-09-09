#pragma once
// ============================================================
// Mesh.h —— GPU 网格（VAO/VBO）的 RAII 封装 + 几何工厂
// 职责：
//   1. 把交错浮点顶点数据上传为 VAO/VBO，析构时自动释放；
//   2. 提供基础几何体（立方体/球/网格/坐标轴/箭头/聚光锥/包围盒线框）的工厂。
// 布局说明：
//   PositionNormal   6 float/顶点（位置+法线）
//   PositionColor    6 float/顶点（位置+颜色）
//   PositionNormalUV 8 float/顶点（位置+法线+UV）
// ============================================================

#include <GL/glew.h>
#include <vector>

class Mesh
{
public:
    enum class Layout
    {
        PositionNormal,    // 6 float：pos.xyz + normal.xyz
        PositionColor,     // 6 float：pos.xyz + color.xyz
        PositionNormalUV   // 8 float：pos.xyz + normal.xyz + uv.xy
    };

    Mesh() = default;
    Mesh(const std::vector<float>& data, Layout layout);

    // 不可拷贝（避免重复持有同一 GL 对象）；可移动。
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    ~Mesh();

    GLuint  Vao()   const { return m_vao; }
    GLsizei Count() const { return m_count; }
    bool    Valid() const { return m_vao != 0; }

    void Bind() const { glBindVertexArray(m_vao); }
    static void Unbind() { glBindVertexArray(0); }

    // 绑定后绘制全部顶点
    void Draw(GLenum mode = GL_TRIANGLES) const;

    // ---- 几何工厂 ----
    static Mesh Cube();
    static Mesh Sphere(int segments = 24, int rings = 12);
    static Mesh Grid();
    static Mesh Axes();
    static Mesh Arrow();
    static Mesh SpotCone();
    static Mesh BoxEdges();

private:
    GLuint  m_vao   = 0;
    GLuint  m_vbo   = 0;
    GLsizei m_count = 0;

    void Upload(const std::vector<float>& data, Layout layout);
    void Release();
};
