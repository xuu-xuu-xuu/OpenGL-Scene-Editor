#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <imm.h>
#pragma comment(lib, "imm32.lib")

#include "Platform.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace
{
HIMC g_savedImeContext = nullptr;
}

bool Platform::PickObjFile(std::string& outPath)
{
    char buffer[MAX_PATH] = { 0 };
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "OBJ Models (*.obj)\0*.obj\0All Files (*.*)\0*.*\0\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = "Select an OBJ model to import";
    if (!GetOpenFileNameA(&ofn)) return false;
    outPath = buffer;
    return true;
}

void Platform::DisableIme(GLFWwindow* window)
{
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) return;
    g_savedImeContext = ImmAssociateContext(hwnd, NULL);
}

void Platform::RestoreIme(GLFWwindow* window)
{
    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd && g_savedImeContext)
        ImmAssociateContext(hwnd, g_savedImeContext);
}

void Platform::SetMouseCapture(GLFWwindow* window)
{
    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd) SetCapture(hwnd);
}

void Platform::ReleaseMouseCapture(GLFWwindow* window)
{
    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd) ReleaseCapture();
}
