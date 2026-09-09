#pragma once
// ============================================================
// Application.h —— 应用编排层
// 职责：
//   1. 初始化 GLFW 窗口 / GLEW / ImGui；
//   2. 注册 GLFW 回调，经 glfwSetWindowUserPointer 转发到本实例；
//   3. 键盘快捷键（O/T/L/X/V/F1/Q/E/WASD/空格/Ctrl/Shift/Esc/Alt/R）；
//   4. 主循环：每帧刷新视口度量 → 渲染 → ImGui 面板 → 交换缓冲；
//   5. 退出清理（在 glfwTerminate 前释放全部 GL 资源）。
// 各子系统通过引用注入（Scene/Camera/RenderSettings/Viewport 为成员，
// Renderer/EditorUI/Picking 在 Run() 内按依赖顺序动态创建）。
// ============================================================

#include <memory>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "Camera.h"
#include "Scene.h"
#include "Settings.h"
#include "Viewport.h"

class Renderer;
class EditorUI;
class Picking;

class Application
{
public:
    Application();
    ~Application();   // 构造/析构均定义在 .cpp，确保 unique_ptr 释放时完整类型可见

    int Run();

private:
    GLFWwindow* m_window = nullptr;
    bool m_imGuiReady = false;

    Camera         m_camera;
    RenderSettings m_settings;
    Scene          m_scene;
    Viewport       m_viewport;

    std::unique_ptr<Renderer>  m_renderer;
    std::unique_ptr<EditorUI>  m_ui;
    std::unique_ptr<Picking>   m_picking;

    // 交互状态
    bool   m_rightMouseDown = false;
    bool   m_firstMouse     = true;
    double m_lastX = 0.0, m_lastY = 0.0;
    double m_lastInputTime = 0.0;   // 最近一次输入动作时间（防呆用）

    // 静态 GLFW 回调 → 通过 user pointer 取回 Application*
    static void CharCallback(GLFWwindow*, unsigned int);
    static void KeyCallback(GLFWwindow*, int, int, int, int);
    static void FramebufferSizeCallback(GLFWwindow*, int, int);
    static void MouseButtonCallback(GLFWwindow*, int, int, int);
    static void CursorPosCallback(GLFWwindow*, double, double);
    static void ScrollCallback(GLFWwindow*, double, double);
    static void DropCallback(GLFWwindow*, int, const char**);
};
