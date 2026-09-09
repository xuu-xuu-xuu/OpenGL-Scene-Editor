#pragma once
// ============================================================
// Picking.h —— 拾取与 Gizmo 拖拽
// 职责：
//   1. 射线拾取（点选光源 / 模型）；
//   2. Gizmo 交互（中心球自由移动、三轴单轴拖动）。
// 通过引用读写 Scene（选择态）与 Camera / Viewport / RenderSettings。
// 所有坐标均为“视口内逻辑坐标”。
// ============================================================

#include <glm/glm.hpp>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "Camera.h"
#include "Scene.h"
#include "Settings.h"
#include "Viewport.h"

class Picking
{
public:
    Picking(Scene& scene, Camera& camera, Viewport& viewport, RenderSettings& settings)
        : m_scene(&scene), m_camera(&camera),
          m_viewport(&viewport), m_settings(&settings) {}

    bool LeftDown() const { return m_leftDown; }

    void OnMouseDown(GLFWwindow* window, double lx, double ly);
    // 返回 true 表示本次移动被拖拽处理（调用方应停止转发给相机旋转）
    bool OnMouseMove(GLFWwindow* window, double lx, double ly);
    void OnMouseUp(GLFWwindow* window);

private:
    Scene*          m_scene;
    Camera*         m_camera;
    Viewport*       m_viewport;
    RenderSettings* m_settings;

    int       m_grabAxis = -1;
    glm::vec3 m_grabStartPos{0.0f};
    glm::vec3 m_grabStartHit{0.0f};
    bool      m_dragMoveEnabled = false;
    bool      m_dragStarted     = false;
    glm::vec3 m_dragPlaneNormal{0.0f, 1.0f, 0.0f};
    glm::vec3 m_lastDragHit{0.0f};
    glm::vec3 m_lastDragPos{0.0f};
    bool      m_leftDown = false;

    void CurrentViewProj(GLFWwindow* window, glm::mat4& view, glm::mat4& proj) const;
    void ScreenToRay(GLFWwindow* window, const glm::mat4& view, const glm::mat4& proj,
                     double mx, double my, glm::vec3& origin, glm::vec3& dir) const;
    void DragRay(GLFWwindow* window, const glm::mat4& view, const glm::mat4& proj,
                 double lx, double ly, glm::vec3& origin, glm::vec3& dir) const;
    int  PickAxis(GLFWwindow* window, double mx, double my) const;
};
