#pragma once
// ============================================================
// EditorUI.h —— ImGui 编辑器面板
// 职责：
//   1. 布局：DockSpace 宿主 + 工具栏 / 层级 / 属性 / 视口 四个面板；
//   2. 读写 Scene（增删模型/灯光、选中）、RenderSettings（风格参数）、
//      Camera（重置相机）；把视口矩形写回 Viewport；
//   3. 视口面板里显示 Renderer 的离屏纹理。
// 通过引用注入依赖，自身不持有窗口或 GL 上下文。
// ============================================================

class Scene;
class Camera;
class Viewport;
class Renderer;
struct RenderSettings;

class EditorUI
{
public:
    EditorUI(Scene& scene, Camera& camera, RenderSettings& settings,
             Viewport& viewport, Renderer& renderer)
        : m_scene(scene), m_camera(camera), m_settings(settings),
          m_viewport(viewport), m_renderer(renderer) {}

    // 每帧调用一次（在 ImGui NewFrame 之后、Render 之前）
    void Draw();

private:
    Scene&          m_scene;
    Camera&         m_camera;
    RenderSettings& m_settings;
    Viewport&       m_viewport;
    Renderer&       m_renderer;

    void DockHostUI();
    void SidebarUI();
    void HierarchyUI();
    void InspectorUI();
    void ViewportUI();
};
