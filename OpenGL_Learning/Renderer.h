#pragma once
// ============================================================
// Renderer.h —— 渲染管线
// 职责：
//   1. 持有全部着色器程序与基础几何体（立方体/球/网格/坐标轴等）；
//   2. 管理离屏渲染目标（场景颜色/深度/法线 + 后处理）；
//   3. Render() 完成 场景 → 法线通道 → 屏幕空间后处理 的全流程。
// 不依赖窗口与 ImGui；所需尺寸通过 Viewport / fbW/fbH 传入。
// ============================================================

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>

#include "Camera.h"
#include "Mesh.h"
#include "Scene.h"
#include "Settings.h"
#include "Shader.h"
#include "Viewport.h"

class Renderer
{
public:
    Renderer();    // 编译着色器、创建原始几何体与离屏目标
    ~Renderer();   // 释放 GL 资源（调用时需 GL 上下文仍有效）

    void Render(const Scene& scene, const Camera& camera,
                const RenderSettings& settings, const Viewport& viewport,
                float time, int fbW, int fbH);

    // 供 ImGui 视口面板显示的最终纹理
    GLuint DisplayTexture() const { return m_postTex ? m_postTex : m_sceneColorTex; }

private:
    // ---- 着色器 ----
    Shader m_world;
    Shader m_line;
    Shader m_sky;
    Shader m_flat;
    Shader m_post;
    Shader m_normal;
    GLuint m_postVao = 0;

    // ---- 原始几何体 ----
    Mesh m_cube;
    Mesh m_sphere;
    Mesh m_grid;
    Mesh m_axes;
    Mesh m_arrow;
    Mesh m_cone;
    Mesh m_boxEdges;

    // 演示场景里的装饰方块（不可选中，仅渲染）
    struct Box { glm::vec3 Pos, Scale, Color; };
    std::vector<Box> m_boxes;

    // ---- 离屏渲染目标 ----
    GLuint m_sceneFbo = 0, m_sceneColorTex = 0, m_sceneDepthTex = 0, m_sceneNormalTex = 0;
    GLuint m_postFbo = 0, m_postTex = 0;
    int    m_fboW = 0, m_fboH = 0;

    void RecreateTarget(int w, int h);
    void ApplyLightUniforms(const Scene& scene);
    void DrawModelRanges(const Shader& sh, const SceneModel& m,
                         const glm::mat4& model, const glm::vec3& color);
    void DrawMesh(const Shader& shader, GLuint vao, GLsizei count,
                  const glm::mat4& model, const glm::vec3& color);
};
