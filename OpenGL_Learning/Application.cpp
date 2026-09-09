#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "Application.h"

#include <GL/glew.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>

#include "EditorUI.h"
#include "Picking.h"
#include "Platform.h"
#include "Renderer.h"
#include "Scene.h"

Application::Application() = default;
Application::~Application() = default;

// ---------- GLFW 回调（静态转发） ----------

void Application::CharCallback(GLFWwindow* window, unsigned int codepoint)
{
    Application* app = (Application*)glfwGetWindowUserPointer(window);
    if (!app) return;
    app->m_lastInputTime = glfwGetTime();
    if (ImGui::GetCurrentContext())
        ImGui_ImplGlfw_CharCallback(window, codepoint);
}

void Application::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    Application* app = (Application*)glfwGetWindowUserPointer(window);
    if (!app) return;
    app->m_lastInputTime = glfwGetTime();
    if (ImGui::GetCurrentContext())
        ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
}

void Application::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void Application::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    Application* app = (Application*)glfwGetWindowUserPointer(window);
    if (!app) return;
    app->m_lastInputTime = glfwGetTime();
    if (ImGui::GetCurrentContext())
        ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

    double hmx, hmy;
    glfwGetCursorPos(window, &hmx, &hmy);
    if (!app->m_viewport.Contains(hmx, hmy))
    {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;
        return;   // 点在 UI 面板上，不操作场景
    }

    if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        if (action == GLFW_PRESS)
        {
            app->m_rightMouseDown = true;
            app->m_firstMouse = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        else if (action == GLFW_RELEASE)
        {
            app->m_rightMouseDown = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    if (!app->m_picking) return;
    double mx = hmx - app->m_viewport.MinX;
    double my = hmy - app->m_viewport.MinY;
    if (action == GLFW_PRESS)
        app->m_picking->OnMouseDown(window, mx, my);
    else if (action == GLFW_RELEASE)
        app->m_picking->OnMouseUp(window);
}

void Application::CursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    Application* app = (Application*)glfwGetWindowUserPointer(window);
    if (!app) return;
    app->m_lastInputTime = glfwGetTime();
    bool leftDown = app->m_picking && app->m_picking->LeftDown();
    if (!app->m_viewport.Contains(xpos, ypos) && !app->m_rightMouseDown && !leftDown)
    {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;
        return;
    }
    double lx = xpos - app->m_viewport.MinX;
    double ly = ypos - app->m_viewport.MinY;
    // 拖拽交给 Picking；返回 true 表示已处理（停止转发给相机旋转）
    if (app->m_picking && app->m_picking->OnMouseMove(window, lx, ly))
        return;

    if (!app->m_rightMouseDown) return;
    if (app->m_firstMouse)
    {
        app->m_lastX = xpos;
        app->m_lastY = ypos;
        app->m_firstMouse = false;
    }
    double mdx = xpos - app->m_lastX;
    double mdy = ypos - app->m_lastY;
    app->m_lastX = xpos;
    app->m_lastY = ypos;
    app->m_camera.Rotate((float)mdx, (float)mdy);
}

void Application::ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    Application* app = (Application*)glfwGetWindowUserPointer(window);
    if (ImGui::GetCurrentContext())
        ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
    if (!app) return;

    double scx, scy;
    glfwGetCursorPos(window, &scx, &scy);
    if (!app->m_viewport.Contains(scx, scy)) return;
    app->m_settings.Fov -= (float)yoffset * 3.0f;
    if (app->m_settings.Fov < 25.0f) app->m_settings.Fov = 25.0f;
    if (app->m_settings.Fov > 95.0f) app->m_settings.Fov = 95.0f;
}

void Application::DropCallback(GLFWwindow* window, int count, const char** paths)
{
    Application* app = (Application*)glfwGetWindowUserPointer(window);
    if (!app) return;
    if (count <= 0 || paths == nullptr) return;
    std::string path = paths[0];
    std::string lower = path;
    for (auto& ch : lower) ch = (char)tolower(ch);
    if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".obj")
    {
        std::cout << "[拖拽] 只支持 .obj：" << path << std::endl;
        return;
    }
    int idx = app->m_scene.AddModelFile(path);
    if (idx >= 0) app->m_scene.SelectModel(idx);
}

// ---------- 主流程 ----------

int Application::Run()
{
    SetConsoleOutputCP(CP_UTF8);   // 控制台 UTF-8 输出，避免中文乱码
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    m_window = glfwCreateWindow(1400, 820, "OpenGL Scene Editor", nullptr, nullptr);
    if (!m_window)
    {
        std::cout << "创建窗口失败" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(m_window);
    glfwSetWindowUserPointer(m_window, this);
    Platform::DisableIme(m_window);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        std::cout << "初始化 GLEW 失败" << std::endl;
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glfwSwapInterval(1);

    glfwSetFramebufferSizeCallback(m_window, FramebufferSizeCallback);
    glfwSetMouseButtonCallback(m_window, MouseButtonCallback);
    glfwSetCursorPosCallback(m_window, CursorPosCallback);
    glfwSetScrollCallback(m_window, ScrollCallback);
    glfwSetDropCallback(m_window, DropCallback);
    glfwSetCharCallback(m_window, CharCallback);
    glfwSetKeyCallback(m_window, KeyCallback);

    m_camera.UpdateVectors();

    // ---- ImGui 初始化（中文字体） ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    bool fontOk = false;
    const char* fontCandidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\msyh.ttf"
    };
    for (const char* fp : fontCandidates)
    {
        if (GetFileAttributesA(fp) != INVALID_FILE_ATTRIBUTES)
        {
            ImFont* font = io.Fonts->AddFontFromFileTTF(fp, 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
            if (!font) continue;
            fontOk = true;
            break;
        }
    }
    if (!fontOk) std::cout << "[ImGui] 未找到中文字体，界面文字可能显示异常" << std::endl;
    ImGui::StyleColorsDark();
    ImGuiStyle& istyle = ImGui::GetStyle();
    istyle.WindowRounding = 6.0f;
    istyle.FrameRounding = 4.0f;
    ImGui_ImplGlfw_InitForOpenGL(m_window, false);
    ImGui_ImplOpenGL3_Init("#version 330");
    m_imGuiReady = true;

    std::cout << "======== OpenGL 场景编辑器 ========\n";
    std::cout << "全部编辑操作可在左侧 ImGui 面板完成；右键拖动转视角/WASD 移动\n";
    std::cout << "====================================\n";

    // ---- 按依赖顺序创建子系统（Renderer 需要 GL 上下文） ----
    m_renderer = std::make_unique<Renderer>();
    m_picking  = std::make_unique<Picking>(m_scene, m_camera, m_viewport, m_settings);
    m_ui       = std::make_unique<EditorUI>(m_scene, m_camera, m_settings, m_viewport, *m_renderer);

    // 初始场景：一盏默认点光源 + 一个圆环
    m_scene.lights.push_back(SceneLight());
    m_scene.AddTorus();

    float lastFrame = (float)glfwGetTime();
    int frameCount = 0;
    float statTimer = 0.0f;

    while (!glfwWindowShouldClose(m_window))
    {
        float now = (float)glfwGetTime();
        float dt = now - lastFrame;
        lastFrame = now;
        if (dt > 0.05f) dt = 0.05f;
        glfwPollEvents();   // 先处理事件，保证 ImGui 输入及时

        // Alt：强制呼出鼠标光标（右键转视角后光标卡住/丢失时使用）
        static bool prevAlt = false;
        bool nowAlt = (glfwGetKey(m_window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) ||
                      (glfwGetKey(m_window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
        if (nowAlt && !prevAlt)
        {
            glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            m_rightMouseDown = false;
            m_firstMouse = true;
            std::cout << "[提示] 已呼出鼠标光标（Alt）" << std::endl;
        }
        prevAlt = nowAlt;

        // 防呆：隐藏光标状态连续 6 秒无任何输入动作则自动恢复
        if (m_rightMouseDown && (now - m_lastInputTime) > 6.0)
        {
            glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            m_rightMouseDown = false;
            m_firstMouse = true;
            std::cout << "[提示] 长时间无操作，已自动恢复鼠标光标" << std::endl;
        }

        if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(m_window, true);

        bool uiCaptureKeys = ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;
        if (!uiCaptureKeys)
        {
            if (glfwGetKey(m_window, GLFW_KEY_R) == GLFW_PRESS)
            {
                m_camera = Camera();
                m_camera.UpdateVectors();
                m_settings.Fov = 50.0f;
            }

            static bool prevO = false;
            bool nowO = glfwGetKey(m_window, GLFW_KEY_O) == GLFW_PRESS;
            if (nowO && !prevO)
            {
                std::string path;
                if (Platform::PickObjFile(path))
                {
                    int idx = m_scene.AddModelFile(path);
                    if (idx >= 0) m_scene.SelectModel(idx);
                }
            }
            prevO = nowO;

            static bool prevT = false;
            bool nowT = glfwGetKey(m_window, GLFW_KEY_T) == GLFW_PRESS;
            if (nowT && !prevT) { int idx = m_scene.AddTorus(); if (idx >= 0) m_scene.SelectModel(idx); }
            prevT = nowT;

            static bool prevL = false;
            bool nowL = glfwGetKey(m_window, GLFW_KEY_L) == GLFW_PRESS;
            if (nowL && !prevL) { int idx = m_scene.AddLight(0, m_camera.Position, m_camera.Front); if (idx >= 0) m_scene.SelectLight(idx); }
            prevL = nowL;

            static bool prevX = false;
            bool nowX = (glfwGetKey(m_window, GLFW_KEY_X) == GLFW_PRESS ||
                         glfwGetKey(m_window, GLFW_KEY_DELETE) == GLFW_PRESS);
            if (nowX && !prevX) m_scene.DeleteSelected();
            prevX = nowX;

            static bool prevV = false;
            bool nowV = glfwGetKey(m_window, GLFW_KEY_V) == GLFW_PRESS;
            if (nowV && !prevV) m_scene.localSpace = !m_scene.localSpace;
            prevV = nowV;

            static bool prevF1 = false;
            bool nowF1 = glfwGetKey(m_window, GLFW_KEY_F1) == GLFW_PRESS;
            if (nowF1 && !prevF1) m_settings.Wireframe = !m_settings.Wireframe;
            prevF1 = nowF1;

            int selModel = m_scene.SelModel();
            if (selModel >= 0 && selModel < (int)m_scene.models.size())
            {
                float rot = 0.0f;
                if (glfwGetKey(m_window, GLFW_KEY_Q) == GLFW_PRESS) rot += 60.0f * dt;
                if (glfwGetKey(m_window, GLFW_KEY_E) == GLFW_PRESS) rot -= 60.0f * dt;
                m_scene.models[selModel].Yaw += rot;
            }

            int fwd = (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS ? 1 : 0)
                    - (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS ? 1 : 0);
            int str = (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS ? 1 : 0)
                    - (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS ? 1 : 0);
            int up  = (glfwGetKey(m_window, GLFW_KEY_SPACE) == GLFW_PRESS ? 1 : 0)
                    - (glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ? 1 : 0);
            bool boost = glfwGetKey(m_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
            m_camera.Move(fwd, str, up, dt, boost);
        }

        // 每帧刷新视口度量（含 16:9 letterbox 矩形），再渲染
        m_viewport.UpdateMetrics(m_window);
        int fbW, fbH;
        glfwGetFramebufferSize(m_window, &fbW, &fbH);
        m_renderer->Render(m_scene, m_camera, m_settings, m_viewport, now, fbW, fbH);

        // ---- ImGui 面板 ----
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        m_ui->Draw();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(m_window);

        frameCount++;
        statTimer += dt;
        if (statTimer >= 0.5f)
        {
            float fps = frameCount / statTimer;
            frameCount = 0;
            statTimer = 0.0f;
            std::string selInfo = "None";
            if (m_scene.SelModel() >= 0) selInfo = "Model: " + m_scene.models[m_scene.SelModel()].Name;
            else if (m_scene.SelLight() >= 0) selInfo = "Light #" + std::to_string(m_scene.SelLight() + 1);
            int lightCount = (int)m_scene.lights.size();
            if (lightCount > kMaxLights) lightCount = kMaxLights;
            char title[512];
            snprintf(title, sizeof(title),
                     "Scene Editor  |  Space: %s  |  Sel: %s  |  Lights: %d/%d  |  FPS: %.0f",
                     m_scene.localSpace ? "Local" : "World", selInfo.c_str(),
                     lightCount, kMaxLights, fps);
            glfwSetWindowTitle(m_window, title);
        }
    }

    // ---- 清理（GL 资源必须在 glfwTerminate 前释放） ----
    m_ui.reset();
    m_picking.reset();
    m_renderer.reset();       // Renderer 析构：释放着色器/几何体/FBO
    m_scene.Clear();          // 释放各模型的 GPU 网格

    if (m_imGuiReady)
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    Platform::RestoreIme(m_window);

    glfwTerminate();
    return 0;
}
