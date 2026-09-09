#pragma once
// ============================================================
// Platform.h —— Windows 平台相关封装
// 职责：文件选择对话框、输入法禁用/恢复、鼠标捕获。
// 集中隔离平台 API，避免散落在输入 / UI 代码里。
// ============================================================

#include <string>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

namespace Platform
{
    // 打开“选择 OBJ 模型”文件对话框，成功返回 true 并写入 outPath
    bool PickObjFile(std::string& outPath);

    // 禁用输入法（避免中文输入法干扰窗口按键）；RestoreIme 恢复
    void DisableIme(GLFWwindow* window);
    void RestoreIme(GLFWwindow* window);

    // 鼠标捕获：拖拽出窗口仍持续跟踪
    void SetMouseCapture(GLFWwindow* window);
    void ReleaseMouseCapture(GLFWwindow* window);
}
