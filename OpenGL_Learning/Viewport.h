#pragma once
// ============================================================
// Viewport.h —— 视口面板度量
// 职责：
//   1. 记录 ImGui“视口”面板的矩形、DPI 缩放与 framebuffer 像素尺寸；
//   2. 计算 16:9 场景在离屏 FBO 内的 letterbox 矩形；
//   3. 提供“光标是否落在场景区 / 面板区”的判定，供拾取与输入使用。
// ============================================================

#include <GL/glew.h>
#include <GLFW/glfw3.h>

class Viewport
{
public:
    // 面板矩形（窗口逻辑坐标，由 EditorUI 在每帧 ImGui 布局后写入）
    double MinX = 0.0, MinY = 0.0;
    int    W = 0, H = 0;

    // DPI 缩放（窗口内容缩放）
    double ScaleX = 1.0, ScaleY = 1.0;
    // 视口对应的 framebuffer 像素尺寸
    int    FBW = 0, FBH = 0;

    // 16:9 场景在 FBO 内的像素矩形（letterbox）
    int    SceneFBX = 0, SceneFBY = 0, SceneFBW = 0, SceneFBH = 0;

    // 由面板逻辑尺寸 + DPI 缩放，刷新 framebuffer 尺寸与 letterbox 矩形
    void UpdateMetrics(GLFWwindow* window);

    // 窗口坐标是否落在视口面板内
    bool Contains(double wx, double wy) const;

    // 视口内逻辑坐标是否落在 16:9 场景区（黑边返回 false）
    bool CursorInScene(double lx, double ly) const;
};
