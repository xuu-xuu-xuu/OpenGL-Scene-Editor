#include "Viewport.h"

namespace
{
constexpr float kSceneAspect = 16.0f / 9.0f;
}

// 由面板逻辑尺寸 + DPI 缩放，计算视口的 framebuffer 像素尺寸
void Viewport::UpdateMetrics(GLFWwindow* window)
{
    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowContentScale(window, &sx, &sy);
    ScaleX = sx > 0.01f ? sx : 1.0f;
    ScaleY = sy > 0.01f ? sy : 1.0f;
    FBW = (W > 0) ? (int)(W * ScaleX) : 0;
    FBH = (H > 0) ? (int)(H * ScaleY) : 0;
    if (W > 0 && FBW < 8) FBW = 8;
    if (H > 0 && FBH < 8) FBH = 8;

    // 16:9 场景在 FBO 内的 letterbox 矩形
    if (FBW > 0 && FBH > 0)
    {
        float panelAspect = (float)FBW / (float)FBH;
        if (panelAspect > kSceneAspect)
        {
            SceneFBH = FBH;
            SceneFBW = (int)(FBH * kSceneAspect);
            SceneFBX = (FBW - SceneFBW) / 2;
            SceneFBY = 0;
        }
        else
        {
            SceneFBW = FBW;
            SceneFBH = (int)(FBW / kSceneAspect);
            SceneFBX = 0;
            SceneFBY = (FBH - SceneFBH) / 2;
        }
    }
}

bool Viewport::Contains(double wx, double wy) const
{
    return wx >= MinX && wy >= MinY &&
           wx < MinX + W && wy < MinY + H;
}

bool Viewport::CursorInScene(double lx, double ly) const
{
    if (SceneFBW <= 0 || SceneFBH <= 0) return false;
    double fx = lx * ScaleX;
    double fy = ly * ScaleY;
    return fx >= SceneFBX && fy >= SceneFBY &&
           fx < SceneFBX + SceneFBW && fy < SceneFBY + SceneFBH;
}
