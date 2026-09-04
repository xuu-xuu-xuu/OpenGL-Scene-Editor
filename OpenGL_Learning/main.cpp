// ============================================================
// OpenGL 场景编辑器（迷你引擎 + ImGui 侧边栏）
// 多模型 / 多光源 / 选择移动 / 世界·本地坐标 Gizmo
//
// 视角: WASD | 空格上 / Ctrl 下 | Shift 加速 | 右键转视角 | 滚轮缩放
// 编辑: 全部可通过左侧 ImGui 面板完成
//       保留快捷键: O 加模型 | T 圆环 | L 加灯 | X 删除 | V 坐标 | Q/E 旋转
// ============================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <imm.h>
#pragma comment(lib, "imm32.lib")

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include "Camera.h"
#include "Shader.h"
#include "ObjLoader.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// ---------- 全局状态 ----------
Camera gCamera;
bool   gRightMouseDown = false;
bool   gFirstMouse = true;
double gLastX = 0.0, gLastY = 0.0;
float  gFov = 50.0f;
bool   gWireframe = false;
bool   gImGuiReady = false;
bool   gShowGrid = true;
bool   gShowSky = true;
bool gSunFollowLight = false;   // 天空太阳是否跟随第一盏灯
bool gToon = true;   // 卡通/赛璐璐着色开关
bool gOutline = true;   // 屏幕空间黑色描边开关
bool gGrade = true;   // 后期调色（降饱和/抬黑位/暗角）
glm::vec3 gShadowTint = glm::vec3(0.42f, 0.38f, 0.62f);   // 暗部色温（偏紫）
float gShadowAmt = 0.35f;
float gBandHi = 0.80f, gBandMid = 0.30f, gBandLo = 0.05f;
glm::vec3 gSpecColor = glm::vec3(1.0f, 0.98f, 0.92f);
float gRimAmt = 0.60f;
float gFaceFill = 0.18f;
float gSatAmt = 0.80f;
float gBloomAmt = 0.12f;
float gGrainAmt = 0.03f;
const int kMaxLights = 8;

struct ModelSubMesh
{
    GLuint TexId = 0;
    bool HasTexture = false;
    bool SoftShade = false;   // 脸/皮肤等材质用柔光
    unsigned int Start = 0;
    unsigned int Count = 0;
};

struct SceneModel
{
    bool Valid = false;
    std::string Name;
    GLuint Vao = 0, Vbo = 0;
    GLsizei Count = 0;
    glm::vec3 Color = glm::vec3(0.9f, 0.72f, 0.4f);
    glm::vec3 Pos = glm::vec3(0.0f, 1.2f, 0.0f);
    float Yaw = 0.0f;
    float Scale = 1.0f;
    float Radius = 1.3f;
    glm::vec3 BoundsMin = glm::vec3(-1.0f);
    glm::vec3 BoundsMax = glm::vec3(1.0f);
    bool Selected = false;
    std::vector<ModelSubMesh> Subs;   // 按材质拆分的子网格
};

struct SceneLight
{
    glm::vec3 Position = glm::vec3(-4.0f, 6.0f, 5.0f);
    glm::vec3 Color = glm::vec3(1.0f, 0.95f, 0.85f);
    float Intensity = 1.6f;
    int Type = 0;   // 0=点光源 1=平行光 2=聚光灯
    glm::vec3 Direction = glm::vec3(0.0f, -1.0f, 0.0f);
    float InnerDeg = 20.0f;
    float OuterDeg = 35.0f;
    bool Selected = false;
};

std::vector<SceneModel> gModels;
std::vector<SceneLight> gLights;
int gSelModel = -1;
int gSelLight = -1;
bool gLocalSpace = false;
int  gGrabAxis = -1;
glm::vec3 gGrabStartPos;
glm::vec3 gGrabStartHit;
double gPressX = 0.0, gPressY = 0.0;
bool gDragMoveEnabled = false;   // 本次按下是否允许拖动（必须点在物体上）
bool gDragStarted = false;       // 是否已越过启动阈值（防止点击瞬移）
glm::vec3 gDragPlaneNormal(0.0f, 1.0f, 0.0f);
glm::vec3 gLastDragHit;
glm::vec3 gLastDragPos;
bool gLeftDown = false;
int  gImportCount = 0;
const float kAxisPickPixels = 14.0f;
HIMC g_SavedImeContext = nullptr;
double gLastInputTime = 0.0;   // 最近一次输入动作时间（防呆用）

// 离屏渲染目标（3D 场景画到这里，再显示在 ImGui 视口面板里）
GLuint gSceneFbo = 0, gSceneColorTex = 0, gSceneDepthRbo = 0;
GLuint gSceneDepthTex = 0;                  // 可采样深度纹理（描边用）
GLuint gSceneNormalTex = 0;
GLuint gPostFbo = 0, gPostTex = 0;          // 后处理输出（描边后画面）
GLuint gPostVao = 0;
int gFboW = 0, gFboH = 0;
double gViewportMinX = 0.0, gViewportMinY = 0.0;   // 视口面板左上角（窗口坐标）
int gViewportW = 0, gViewportH = 0;                // 视口面板尺寸

double gViewportScaleX = 1.0, gViewportScaleY = 1.0;   // 窗口内容缩放（DPI）
int gViewportFBW = 0, gViewportFBH = 0;                // 视口对应的 framebuffer 像素尺寸

int gSceneFBX = 0, gSceneFBY = 0, gSceneFBW = 0, gSceneFBH = 0;  // 16:9 场景在 FBO 内的像素矩形
const float kSceneAspect = 16.0f / 9.0f;
bool ViewportCursorInScene(double lx, double ly);   // 前置声明（定义在下方）

// ---------- 着色器 ----------
const char* worldVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
out vec2 vUV;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMat;
out vec3 vWorldPos;
out vec3 vNormal;
void main()
{
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = vec3(world);
    vNormal   = uNormalMat * aNormal;
    vUV = aUV;
    gl_Position = uProj * uView * world;
}
)";

const char* worldFragSrc = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
out vec4 FragColor;

uniform vec3 uColor;
uniform sampler2D uTex;
uniform int uUseTex;
uniform int uSoftShade;
uniform vec3 uViewPos;
uniform float uShininess;
uniform float uToon;
uniform float uBandHi;
uniform float uBandMid;
uniform float uBandLo;
uniform vec3 uShadowTint;
uniform float uShadowAmt;
uniform vec3 uSpecColor;
uniform float uRimAmt;
uniform float uFaceFill;
const int kMaxLights = 8;
uniform int uLightCount;
uniform vec3 uLightPos[kMaxLights];
uniform vec3 uLightColor[kMaxLights];
uniform float uLightIntensity[kMaxLights];
uniform float uLightTypeF[kMaxLights];
uniform vec3 uLightDir[kMaxLights];
uniform float uLightConeIn[kMaxLights];
uniform float uLightConeOut[kMaxLights];

void main()
{
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 baseColor = (uUseTex > 0) ? texture(uTex, vUV).rgb : uColor;
    vec3 result = (uToon > 0.5f)
        ? vec3(0.20f, 0.19f, 0.21f) * baseColor
        : (0.16f + 0.08f * max(N.y, 0.0f)) * baseColor;
    for (int i = 0; i < kMaxLights; ++i)
    {
        if (i >= uLightCount) break;

        vec3 L;
        float atten = 1.0f;
        if (uLightTypeF[i] < 0.5f)
        {
            // 点光源：距离衰减
            vec3 toL = uLightPos[i] - vWorldPos;
            float dist = length(toL);
            atten = 1.0f / (1.0f + 0.09f * dist + 0.05f * dist * dist);
            if (atten < 0.18f) atten = 0.18f;
            L = normalize(toL);
        }
        else if (uLightTypeF[i] < 1.5f)
        {
            // 平行光：只取方向，无衰减
            L = normalize(uLightDir[i]);
        }
        else
        {
            // 聚光灯：距离衰减 + 锥角衰减
            vec3 toL = uLightPos[i] - vWorldPos;
            float dist = length(toL);
            atten = 1.0f / (1.0f + 0.09f * dist + 0.05f * dist * dist);
            if (atten < 0.18f) atten = 0.18f;
            L = normalize(toL);
            float cosA = max(dot(-L, normalize(uLightDir[i])), 0.0f);
            atten *= smoothstep(uLightConeOut[i], uLightConeIn[i], cosA);
        }

        vec3 H = normalize(L + V);
        float ndl = max(dot(N, L), 0.0);
        float diff = ndl;
        if (uToon > 0.5f)
        {
            if (uSoftShade > 0)
            {
                // 脸/皮肤：柔和受光，暗部不会死黑
                diff = 0.48f + 0.40f * smoothstep(0.08f, 0.55f, ndl);   // 脸：与身体亮度协调
            }
            else
            {
                float bLo = mix(0.14f, 0.32f, smoothstep(uBandLo - 0.015f, uBandLo + 0.015f, ndl));
                float bMid = mix(bLo, 0.64f, smoothstep(uBandMid - 0.025f, uBandMid + 0.025f, ndl));
                diff = mix(bMid, 1.0f, smoothstep(uBandHi - 0.03f, uBandHi + 0.03f, ndl));
            }
        }
        float ndh = max(dot(N, H), 0.0);
        float spec = pow(ndh, uShininess);
        if (uToon > 0.5f)
            spec = smoothstep(0.36f, 0.48f, ndh) * pow(ndh, 240.0f) * 0.7f;
            spec = smoothstep(0.46f, 0.56f, ndh) * pow(ndh, 340.0f) * 0.55f;   // 高光更小更硬
            if (uSoftShade > 0) spec *= 0.25f;   // 脸/皮肤哑光，去塑料感

        vec3 radiance = uLightColor[i] * uLightIntensity[i] * atten;
                result += radiance * (diff * baseColor + spec * uSpecColor);
        float darkAmt = (uToon > 0.5f) ? clamp(1.0f - diff, 0.0f, 1.0f) : 0.0f;
        result += radiance * uShadowTint * darkAmt * uShadowAmt * 0.35f;
    }
    if (uToon > 0.5f)
    {
        float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
        result += vec3(0.32f, 0.42f, 0.62f) * rim * uRimAmt;
        result += vec3(0.35f, 0.50f, 0.80f) * rim * uRimAmt;
        // 脸部补光：面向相机方向提亮，抹平生硬阴影
        if (uSoftShade > 0)
            result += baseColor * max(dot(N, V), 0.0) * uFaceFill * 0.30f;
    }
    FragColor = vec4(result, 1.0);
}
)";

const char* lineVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vColor;
void main()
{
    vColor = aColor;
    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
}
)";

const char* lineFragSrc = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main()
{
    FragColor = vec4(vColor, 1.0);
}
)";

const char* skyVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uProj;
uniform mat4 uView;
out vec3 vDir;
void main()
{
    vDir = aPos;
    mat4 viewRot = mat4(mat3(uView));
    vec4 pos = uProj * viewRot * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
}
)";

const char* skyFragSrc = R"(
#version 330 core
in vec3 vDir;
out vec4 FragColor;
uniform vec3 uSunDir;
void main()
{
    vec3 dir = normalize(vDir);
    float h = clamp(dir.y, -1.0, 1.0);
    vec3 zenith  = vec3(0.10f, 0.26f, 0.52f);
    vec3 horizon = vec3(0.68f, 0.80f, 0.88f);
    vec3 ground  = vec3(0.15f, 0.15f, 0.17f);
    vec3 col;
    if (dir.y < 0.0)
        col = mix(ground, horizon, smoothstep(-0.25f, 0.0f, h));
    else
        col = mix(horizon, zenith, pow(h, 0.45f));
    float sun = max(dot(dir, normalize(uSunDir)), 0.0);
    col += vec3(1.0f, 0.95f, 0.82f) * (pow(sun, 800.0f) * 1.4f + pow(sun, 24.0f) * 0.30f);
    FragColor = vec4(col, 1.0);
}
)";

const char* flatVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uMvp;
void main()
{
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

const char* flatFragSrc = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
uniform sampler2D uTex;
uniform int uUseTex;
uniform int uSoftShade;
void main()
{
    FragColor = vec4(uColor, 1.0);
}
)";

// 屏幕空间描边后处理（全屏三角形）
const char* postVertSrc = R"(
#version 330 core
out vec2 vUv;
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* postFragSrc = R"(
#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform float uOutlineOn;
uniform float uGradeOn;
uniform float uSatAmt;
uniform float uTime;
uniform float uBloomAmt;
uniform float uGrainAmt;

float toLinear(float z)
{
    const float n = 0.05;
    const float f = 500.0;
    return z;   // 原始非线性深度：远处深度差小，避免远处整片被当描边涂黑
}

void main()
{
    vec3 col = texture(uScene, vUv).rgb;
    if (uOutlineOn > 0.5)
    {
        vec2 texel = 1.0 / vec2(textureSize(uDepth, 0));
        float c = toLinear(texture(uDepth, vUv).r);
        float l = toLinear(texture(uDepth, vUv - vec2(texel.x, 0.0)).r);
        float r = toLinear(texture(uDepth, vUv + vec2(texel.x, 0.0)).r);
        float u = toLinear(texture(uDepth, vUv + vec2(0.0, texel.y)).r);
        float d = toLinear(texture(uDepth, vUv - vec2(0.0, texel.y)).r);
        float mag = abs(l - r) + abs(u - d);
        float edgeD = smoothstep(0.004, 0.02, mag);   // 非线性深度阈值
        vec3 nC = texture(uNormal, vUv).rgb * 2.0 - 1.0;
        vec3 nL = texture(uNormal, vUv - vec2(texel.x, 0.0)).rgb * 2.0 - 1.0;
        vec3 nR = texture(uNormal, vUv + vec2(texel.x, 0.0)).rgb * 2.0 - 1.0;
        vec3 nU = texture(uNormal, vUv + vec2(0.0, texel.y)).rgb * 2.0 - 1.0;
        vec3 nD = texture(uNormal, vUv - vec2(0.0, texel.y)).rgb * 2.0 - 1.0;
                float nl = (c < 0.995f && l < 0.995f) ? 1.0f : 0.0f;   // 左右两侧都是几何体才允许法线折痕
        float nu = (c < 0.995f && u < 0.995f) ? 1.0f : 0.0f;
        float magN = length(nR - nL) * nl + length(nU - nD) * nu;
        float edgeN = smoothstep(0.18, 0.60, magN);
        float edge = max(edgeD, edgeN * 0.45);
        float distFade = 1.0f - smoothstep(0.55f, 0.92f, c);   // 距离淡出描边
        col = mix(col, vec3(0.08, 0.09, 0.16), edge * 0.50f * distFade);   // 深蓝灰描边
    }
    if (uGradeOn > 0.5)
    {
        float luma = dot(col, vec3(0.299, 0.587, 0.114));
        col = mix(vec3(luma), col, uSatAmt);
        col = col * 0.97 + 0.028;
        col *= vec3(1.015f, 1.0f, 0.965f);   // 轻微暖调，统一画面

        vec2 gtex = 1.0 / vec2(textureSize(uDepth, 0));
        float gc = texture(uDepth, vUv).r;
        float gl = texture(uDepth, vUv - vec2(gtex.x, 0.0)).r;
        float gr = texture(uDepth, vUv + vec2(gtex.x, 0.0)).r;
        float gu = texture(uDepth, vUv + vec2(0.0, gtex.y)).r;
        float gd = texture(uDepth, vUv - vec2(0.0, gtex.y)).r;
        float lap = abs(2.0 * gc - gl - gr) + abs(2.0 * gc - gu - gd);
        col *= 1.0 - smoothstep(0.003, 0.025, lap) * 0.22;   // 凹处伪AO

        // 柔和泛光（4 方向采样，展开写避免解析问题）
        vec2 d1 = vec2(gtex.x * 2.0, 0.0);
        vec2 d2 = vec2(0.0, gtex.y * 2.0);
        vec3 glow = vec3(0.0);
        glow += max(texture(uScene, vUv - d1).rgb - vec3(0.85), vec3(0.0));
        glow += max(texture(uScene, vUv + d1).rgb - vec3(0.85), vec3(0.0));
        glow += max(texture(uScene, vUv - d2).rgb - vec3(0.85), vec3(0.0));
        glow += max(texture(uScene, vUv + d2).rgb - vec3(0.85), vec3(0.0));
        col += (glow * 0.25) * uBloomAmt;
        // 噪点颗粒
        float nh = fract(sin(dot(vUv * 100.0 + uTime, vec2(12.9898, 78.233))) * 43758.5453);
        col += (nh - 0.5) * uGrainAmt;
        vec2 q = vUv - 0.5;
        float vig = smoothstep(0.45, 1.15, length(q) * 1.45);
        col *= 1.0 - vig * 0.38;   // 暗角
    }
    FragColor = vec4(col, 1.0);
}
)";

// 法线通道：把视空间法线写入场景 FBO 的 COLOR_ATTACHMENT1
const char* normalVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMat;
out vec3 vViewNormal;
void main()
{
    vec4 world = uModel * vec4(aPos, 1.0);
    vViewNormal = mat3(uView) * uNormalMat * aNormal;
    gl_Position = uProj * uView * world;
}
)";

const char* normalFragSrc = R"(
#version 330 core
in vec3 vViewNormal;
out vec4 FragColor;
void main()
{
    vec3 n = normalize(vViewNormal);
    FragColor = vec4(n * 0.5 + 0.5, 1.0);
}
)";
// ---------- 工具 ----------
bool PickObjFile(std::string& outPath)
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

std::string FileNameOf(const std::string& path)
{
    size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

void DisableImeForWindow(GLFWwindow* window)
{
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) return;
    g_SavedImeContext = ImmAssociateContext(hwnd, NULL);
}

void UploadMesh(const std::vector<float>& data, GLuint& vao, GLuint& vbo)
{
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(data.size() * sizeof(float)),
                 data.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void DestroyMesh(GLuint& vao, GLuint& vbo)
{
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    vao = 0; vbo = 0;
}

// 8 float/顶点（位置+法线+UV）上传
void UploadMeshUV(const std::vector<float>& data, GLuint& vao, GLuint& vbo)
{
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(data.size() * sizeof(float)),
                 data.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

// 用 stb_image 加载贴图（成功返回纹理 id）
GLuint LoadTextureFile(const std::string& path)
{
    stbi_set_flip_vertically_on_load(true);
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) return 0;
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return id;
}

// 简单纹理缓存：同一路径只加载一次
GLuint LoadTextureCached(const std::string& path)
{
    static std::vector<std::pair<std::string, GLuint>> cache;
    for (auto& kv : cache) if (kv.first == path) return kv.second;
    GLuint id = LoadTextureFile(path);
    if (id) cache.push_back(std::make_pair(path, id));
    return id;
}

// 按材质子网格绘制
void DrawModelRanges(const Shader& sh, const SceneModel& m,
                     const glm::mat4& model, const glm::vec3& color)
{
    sh.SetMat4("uModel", model);
    glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(model)));
    sh.SetMat3("uNormalMat", normalMat);
    sh.SetVec3("uColor", color);
    glBindVertexArray(m.Vao);
    if (m.Subs.empty())
    {
        sh.SetInt("uUseTex", 0);
        sh.SetInt("uSoftShade", 0);
        glDrawArrays(GL_TRIANGLES, 0, m.Count);
    }
    else
    {
        for (const ModelSubMesh& sm : m.Subs)
        {
            sh.SetInt("uUseTex", sm.HasTexture ? 1 : 0);
            sh.SetInt("uSoftShade", sm.SoftShade ? 1 : 0);
            if (sm.HasTexture)
            {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, sm.TexId);
            }
            glDrawArrays(GL_TRIANGLES, sm.Start, sm.Count);
        }
    }
    glBindVertexArray(0);
}

// 带贴图绘制（uUseTex=1，绑定 unit0）
void DrawTexturedMesh(const Shader& shader, GLuint vao, GLsizei count,
                      const glm::mat4& model, const glm::vec3& color, GLuint tex)
{
    shader.SetMat4("uModel", model);
    glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(model)));
    shader.SetMat3("uNormalMat", normalMat);
    shader.SetVec3("uColor", color);
    shader.SetInt("uUseTex", 1);
    shader.SetInt("uSoftShade", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, count);
    glBindVertexArray(0);
}

bool LoadAndCenterObj(const std::string& path, ObjModel& out)
{
    if (!LoadObjFile(path, out)) return false;
    glm::vec3 center = out.Center();
    float radius = out.Radius();
    float fit = (radius > 0.0001f) ? 2.6f / (2.0f * radius) : 1.0f;
    glm::vec3 oldMin = out.BoundsMin;
    glm::vec3 oldMax = out.BoundsMax;
    for (size_t i = 0; i < out.Data.size(); i += 8)
    {
        glm::vec3 p(out.Data[i], out.Data[i + 1], out.Data[i + 2]);
        p = (p - center) * fit;
        out.Data[i] = p.x;
        out.Data[i + 1] = p.y;
        out.Data[i + 2] = p.z;
    }
    out.BoundsMin = (oldMin - center) * fit;
    out.BoundsMax = (oldMax - center) * fit;
    return true;
}

int AddModelFile(const std::string& path)
{
    ObjModel obj;
    if (!LoadAndCenterObj(path, obj)) return -1;
    SceneModel m;
    m.Name = FileNameOf(path);
    m.Valid = true;
    m.Count = (GLsizei)obj.VertexCount;
    m.Radius = obj.Radius();
    m.BoundsMin = obj.BoundsMin;
    m.BoundsMax = obj.BoundsMax;
    UploadMeshUV(obj.Data, m.Vao, m.Vbo);

    // ---- 定位 .mtl ----
    std::string dir = path.substr(0, path.find_last_of("/\\") + 1);
    std::string mtlName;
    {
        std::ifstream objf(path);
        std::string l;
        while (std::getline(objf, l))
        {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            std::istringstream ls(l);
            std::string key;
            ls >> key;
            if (key == "mtllib") { std::getline(ls, mtlName); break; }
        }
        size_t b = mtlName.find_first_not_of(" \t\"");
        if (b != std::string::npos) mtlName = mtlName.substr(b);
        size_t en = mtlName.find_last_not_of(" \t\"");
        if (en != std::string::npos) mtlName = mtlName.substr(0, en + 1);
    }
    if (mtlName.empty())
    {
        size_t dot = path.find_last_of('.');
        mtlName = (dot == std::string::npos) ? path + ".mtl" : path.substr(0, dot) + ".mtl";
        if (mtlName.find_first_of("/\\") == std::string::npos) mtlName = dir + mtlName;
    }
    else
    {
        bool absPath = (mtlName.size() >= 3 && isalpha((unsigned char)mtlName[0]) && mtlName[1] == ':') ||
                       (!mtlName.empty() && (mtlName[0] == '/' || mtlName[0] == '\\'));
        if (!absPath)
        {
            if (mtlName.rfind("./", 0) == 0 || mtlName.rfind(".\\", 0) == 0)
                mtlName = mtlName.substr(2);
            mtlName = dir + mtlName;
        }
    }

    // ---- 解析所有 newmtl 的 map_Kd ----
    std::map<std::string, std::string> texByMat;
    {
        std::ifstream mtl(mtlName);
        std::string ml;
        std::string curMat;
        while (std::getline(mtl, ml))
        {
            if (!ml.empty() && ml.back() == '\r') ml.pop_back();
            std::istringstream ms(ml);
            std::string key;
            ms >> key;
            if (key == "newmtl") { ms >> curMat; }
            else if (key == "map_Kd" && !curMat.empty())
            {
                std::string rel;
                std::getline(ms, rel);
                size_t b2 = rel.find_first_not_of(" \t\"");
                if (b2 != std::string::npos) rel = rel.substr(b2);
                size_t e2 = rel.find_last_not_of(" \t\"");
                if (e2 != std::string::npos) rel = rel.substr(0, e2 + 1);
                texByMat[curMat] = rel;
            }
        }
    }

    // ---- 按材质分段建子网格 ----
    for (const ObjMatRange& r : obj.Materials)
    {
        ModelSubMesh sm;
        sm.HasTexture = false;
        auto it = texByMat.find(r.Material);
        std::string mn = r.Material;
        for (auto& ch : mn) ch = (char)tolower(ch);
        if (mn.find("face") != std::string::npos || mn.find("skin") != std::string::npos ||
            mn.find("cheek") != std::string::npos || mn.find("head") != std::string::npos)
            sm.SoftShade = true;
        if (it != texByMat.end() && !it->second.empty())
        {
            std::string texRel = it->second;
            std::vector<std::string> tries;
            if (texRel.size() >= 2 && texRel[1] == ':') tries.push_back(texRel);
            tries.push_back(dir + texRel);
            size_t slash = texRel.find_last_of("/\\");
            if (slash != std::string::npos) tries.push_back(dir + texRel.substr(slash + 1));
            for (const std::string& cand : tries)
            {
                GLuint tid = LoadTextureCached(cand);
                if (tid) { sm.TexId = tid; sm.HasTexture = true; break; }
            }
        }
        sm.Start = r.Start;
        sm.Count = r.Count;
        m.Subs.push_back(sm);
    }
    if (m.Subs.empty())
    {
        ModelSubMesh sm;
        sm.Start = 0;
        sm.Count = obj.VertexCount;
        m.Subs.push_back(sm);
    }

    const glm::vec3 palette[] = {
        glm::vec3(0.90f, 0.72f, 0.40f), glm::vec3(0.55f, 0.75f, 0.95f),
        glm::vec3(0.85f, 0.45f, 0.45f), glm::vec3(0.55f, 0.85f, 0.60f),
        glm::vec3(0.75f, 0.60f, 0.90f)
    };
    m.Color = palette[gImportCount % 5];
    gImportCount++;
    float a = (float)gModels.size() * 2.39996f;
    m.Pos = glm::vec3(cosf(a) * 2.5f, 1.2f, sinf(a) * 2.5f);
    gModels.push_back(m);
    std::cout << "[添加模型] " << m.Name << "（" << obj.VertexCount << " 顶点）" << std::endl;
    return (int)gModels.size() - 1;
}

int AddTorus()
{
    const char* candidates[] = {
        "models/torus.obj", "../../models/torus.obj",
        "D:/OpenGL_Learning/OpenGL_Learning/OpenGL_Learning/models/torus.obj"
    };
    for (const char* p : candidates)
    {
        int idx = AddModelFile(p);
        if (idx >= 0) return idx;
    }
    std::cout << "[提示] 找不到 torus.obj" << std::endl;
    return -1;
}

int AddLight(int type = 0)
{
    if ((int)gLights.size() >= kMaxLights)
    {
        std::cout << "[光源] 已达上限 " << kMaxLights << "，请先删除" << std::endl;
        return -1;
    }
    SceneLight l;
    l.Type = type;
    if (type == 1) l.Direction = glm::normalize(glm::vec3(0.35f, 0.8f, 0.45f));
    if (type == 2) l.Direction = glm::vec3(0.0f, -1.0f, 0.0f);
    l.Position = gCamera.Position + gCamera.Front * 6.0f;
    l.Position.y = std::max(l.Position.y, 1.0f);
    gLights.push_back(l);
    int idx = (int)gLights.size() - 1;
    std::cout << "[添加光源] 第 " << idx + 1 << " 盏" << std::endl;
    return idx;
}

// ---------- 选择管理 ----------
void ClearSelection()
{
    if (gSelModel >= 0 && gSelModel < (int)gModels.size()) gModels[gSelModel].Selected = false;
    if (gSelLight >= 0 && gSelLight < (int)gLights.size()) gLights[gSelLight].Selected = false;
    gSelModel = -1;
    gSelLight = -1;
    gGrabAxis = -1;
}

void SelectModel(int i)
{
    ClearSelection();
    if (i >= 0 && i < (int)gModels.size())
    {
        gSelModel = i;
        gModels[i].Selected = true;
    }
}

void SelectLight(int i)
{
    ClearSelection();
    if (i >= 0 && i < (int)gLights.size())
    {
        gSelLight = i;
        gLights[i].Selected = true;
    }
}

bool HasSelection() { return gSelModel >= 0 || gSelLight >= 0; }

glm::vec3 SelPos()
{
    if (gSelModel >= 0) return gModels[gSelModel].Pos;
    if (gSelLight >= 0) return gLights[gSelLight].Position;
    return glm::vec3(0.0f);
}

void SetSelPos(const glm::vec3& p)
{
    if (gSelModel >= 0) gModels[gSelModel].Pos = p;
    if (gSelLight >= 0) gLights[gSelLight].Position = p;
}

float SelGizmoLen() { return (gSelModel >= 0) ? 2.2f : 1.3f; }

glm::vec3 AxisDirWorld(int axis)
{
    glm::vec3 e = (axis == 0) ? glm::vec3(1, 0, 0)
                : (axis == 1) ? glm::vec3(0, 1, 0)
                              : glm::vec3(0, 0, 1);
    if (gLocalSpace && gSelModel >= 0)
    {
        float yaw = glm::radians(gModels[gSelModel].Yaw);
        glm::mat4 r = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0));
        e = glm::vec3(r * glm::vec4(e, 0.0f));
    }
    return e;
}

void DeleteSelected()
{
    if (gSelModel >= 0 && gSelModel < (int)gModels.size())
    {
        DestroyMesh(gModels[gSelModel].Vao, gModels[gSelModel].Vbo);
        std::cout << "[删除模型] " << gModels[gSelModel].Name << std::endl;
        gModels.erase(gModels.begin() + gSelModel);
        ClearSelection();
    }
    else if (gSelLight >= 0 && gSelLight < (int)gLights.size())
    {
        std::cout << "[删除光源] 第 " << gSelLight + 1 << " 盏" << std::endl;
        gLights.erase(gLights.begin() + gSelLight);
        ClearSelection();
    }
    else std::cout << "[删除] 当前没有选中任何物体" << std::endl;
}
// ---------- 拾取数学 ----------
void CurrentViewProj(GLFWwindow* window, glm::mat4& view, glm::mat4& proj)
{
    view = gCamera.GetViewMatrix();
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
            float aspect = (gSceneFBH > 0) ? (float)gSceneFBW / (float)gSceneFBH : ((fbH > 0) ? (float)fbW / (float)fbH : 1.0f);
    proj = glm::perspective(glm::radians(gFov), aspect, 0.05f, 500.0f);
}

void ScreenToRay(GLFWwindow* window, const glm::mat4& view, const glm::mat4& proj,
                 double mx, double my, glm::vec3& origin, glm::vec3& dir)
{
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    origin = gCamera.Position;
    if (h <= 0) { dir = gCamera.Front; return; }
    glm::vec4 viewport(0.0f, 0.0f, (float)w, (float)h);
    glm::vec3 nearP = glm::unProject(glm::vec3((float)mx, (float)(h - my), 0.0f), view, proj, viewport);
    glm::vec3 farP  = glm::unProject(glm::vec3((float)mx, (float)(h - my), 1.0f), view, proj, viewport);
    dir = glm::normalize(farP - nearP);
}

// 拖动专用射线：允许鼠标越过黑边/场景矩形继续延伸，避免快速拖动“脱手”
void DragRay(GLFWwindow* window, const glm::mat4& view, const glm::mat4& proj,
             double lx, double ly, glm::vec3& origin, glm::vec3& dir)
{
    origin = gCamera.Position;
    if (gSceneFBW <= 0 || gSceneFBH <= 0) { dir = gCamera.Front; return; }
    float fx = (float)(lx * gViewportScaleX);
    float fy = (float)(ly * gViewportScaleY);
    float sx = fx - gSceneFBX;
    float sy = fy - gSceneFBY;
    glm::vec4 viewport(0.0f, 0.0f, (float)gSceneFBW, (float)gSceneFBH);
    glm::vec3 nearP = glm::unProject(glm::vec3(sx, (float)gSceneFBH - sy, 0.0f), view, proj, viewport);
    glm::vec3 farP  = glm::unProject(glm::vec3(sx, (float)gSceneFBH - sy, 1.0f), view, proj, viewport);
    dir = glm::normalize(farP - nearP);
}
bool RaySphereIntersect(const glm::vec3& origin, const glm::vec3& dir,
                        const glm::vec3& center, float radius, float& outT)
{
    glm::vec3 oc = origin - center;
    float b = glm::dot(oc, dir);
    float c = glm::dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0f) return false;
    float sq = sqrtf(disc);
    float t1 = -b - sq;
    float t2 = -b + sq;
    if (t1 > 0.001f) { outT = t1; return true; }
    if (t2 > 0.001f) { outT = t2; return true; }
    return false;
}

bool RayPlaneIntersect(const glm::vec3& origin, const glm::vec3& dir,
                       const glm::vec3& point, const glm::vec3& normal, float& outT)
{
    float denom = glm::dot(dir, normal);
    if (fabsf(denom) < 1e-5f) return false;
    outT = glm::dot(point - origin, normal) / denom;
    return outT > 0.0f;
}

bool WorldToScreen(GLFWwindow* window, const glm::mat4& view, const glm::mat4& proj,
                   const glm::vec3& world, double& sx, double& sy)
{
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    if (h <= 0) return false;
    glm::vec4 clip = proj * view * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0001f) return false;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) return false;
    sx = (ndc.x * 0.5f + 0.5f) * w;
    sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * h;
    return true;
}

float PointSegmentDist2D(double px, double py,
                         double x0, double y0, double x1, double y1)
{
    double dx = x1 - x0, dy = y1 - y0;
    double len2 = dx * dx + dy * dy;
    double t = (len2 > 0.0) ? ((px - x0) * dx + (py - y0) * dy) / len2 : 0.0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    double cx = x0 + t * dx, cy = y0 + t * dy;
    return (float)sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
}

int PickAxis(GLFWwindow* window, double mx, double my)
{
    if (!HasSelection()) return -1;
    if (!ViewportCursorInScene(mx, my)) return -1;

    glm::mat4 view, proj;
    CurrentViewProj(window, view, proj);
    glm::vec3 center = SelPos();
    float len = SelGizmoLen();

    // 场景内 framebuffer 像素坐标（相对场景矩形左上角）
    double cx = mx * gViewportScaleX - gSceneFBX;
    double cy = my * gViewportScaleY - gSceneFBY;

    auto projectScene = [&](const glm::vec3& w, double& sx, double& sy) -> bool
    {
        glm::vec4 clip = proj * view * glm::vec4(w, 1.0f);
        if (clip.w <= 0.0001f) return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < -1.0f || ndc.z > 1.0f) return false;
        sx = (ndc.x * 0.5f + 0.5f) * gSceneFBW;
        sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * gSceneFBH;
        return true;
    };

    float tol = kAxisPickPixels * (float)gViewportScaleX;
    for (int axis = 0; axis < 3; ++axis)
    {
        glm::vec3 tip = center + AxisDirWorld(axis) * len;
        double x0, y0, x1, y1;
        if (!projectScene(center, x0, y0)) continue;
        if (!projectScene(tip, x1, y1)) continue;
        if (PointSegmentDist2D(cx, cy, x0, y0, x1, y1) <= tol)
            return axis;
    }
    return -1;
}

// 射线 vs AABB（slab 法）
bool RayAABB(const glm::vec3& o, const glm::vec3& d,
             const glm::vec3& bmin, const glm::vec3& bmax, float& outT)
{
    float t0 = 0.0f, t1 = 1e30f;
    for (int a = 0; a < 3; ++a)
    {
        float oa = a == 0 ? o.x : (a == 1 ? o.y : o.z);
        float da = a == 0 ? d.x : (a == 1 ? d.y : d.z);
        float mn = a == 0 ? bmin.x : (a == 1 ? bmin.y : bmin.z);
        float mx = a == 0 ? bmax.x : (a == 1 ? bmax.y : bmax.z);
        if (fabsf(da) < 1e-6f)
        {
            if (oa < mn || oa > mx) return false;
        }
        else
        {
            float inv = 1.0f / da;
            float ta = (mn - oa) * inv;
            float tb = (mx - oa) * inv;
            if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
            t0 = ta > t0 ? ta : t0;
            t1 = tb < t1 ? tb : t1;
            if (t0 > t1) return false;
        }
    }
    outT = t0 > 0.0f ? t0 : t1;
    return outT > 0.001f;
}

void ModelWorldAABB(const SceneModel& m, glm::vec3& bmin, glm::vec3& bmax)
{
    glm::mat4 M(1.0f);
    M = glm::translate(M, m.Pos);
    M = glm::rotate(M, glm::radians(m.Yaw), glm::vec3(0, 1, 0));
    M = glm::scale(M, glm::vec3(m.Scale));
    bmin = glm::vec3(1e30f);
    bmax = glm::vec3(-1e30f);
    for (int i = 0; i < 8; ++i)
    {
        glm::vec3 c(0.0f);
        c.x = (i & 1) ? m.BoundsMin.x : m.BoundsMax.x;
        c.y = (i & 2) ? m.BoundsMin.y : m.BoundsMax.y;
        c.z = (i & 4) ? m.BoundsMin.z : m.BoundsMax.z;
        glm::vec3 w = glm::vec3(M * glm::vec4(c, 1.0f));
        bmin = glm::min(bmin, w);
        bmax = glm::max(bmax, w);
    }
}

std::vector<float> MakeBoxEdgesData()
{
    std::vector<float> data;
    auto line = [&](float x0, float y0, float z0, float x1, float y1, float z1)
    {
        data.push_back(x0); data.push_back(y0); data.push_back(z0);
        data.push_back(1.0f); data.push_back(0.55f); data.push_back(0.1f);
        data.push_back(x1); data.push_back(y1); data.push_back(z1);
        data.push_back(1.0f); data.push_back(0.55f); data.push_back(0.1f);
    };
    const float h = 0.5f;
    for (int i = -1; i <= 1; i += 2)
    {
        for (int j = -1; j <= 1; j += 2)
        {
            float x = i * h, y = j * h;
            line(x, y, -h, x, y, h);
            line(x, -h, y, x, h, y);
            line(-h, x, y, h, x, y);
        }
    }
    return data;
}
// ---------- ImGui 事件转发 ----------
void CharCallback(GLFWwindow* window, unsigned int codepoint)
{
    gLastInputTime = glfwGetTime();
    if (ImGui::GetCurrentContext())
        ImGui_ImplGlfw_CharCallback(window, codepoint);
}

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    gLastInputTime = glfwGetTime();
    if (ImGui::GetCurrentContext())
        ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
}

// ---------- 回调 ----------
void FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    gLastInputTime = glfwGetTime();
    if (ImGui::GetCurrentContext())
        ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

    double hmx, hmy;
    glfwGetCursorPos(window, &hmx, &hmy);
    bool mouseInViewport = (hmx >= gViewportMinX && hmy >= gViewportMinY &&
                            hmx < gViewportMinX + gViewportW && hmy < gViewportMinY + gViewportH);
    if (!mouseInViewport)
    {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;
        return;   // 点在 UI 面板上，不操作场景
    }

    if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        if (action == GLFW_PRESS)
        {
            gRightMouseDown = true;
            gFirstMouse = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        else if (action == GLFW_RELEASE)
        {
            gRightMouseDown = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    if (action == GLFW_PRESS)
    {
        gLeftDown = true;
        gDragMoveEnabled = false;
        gDragStarted = false;
        glfwGetCursorPos(window, &gPressX, &gPressY);
        double mx = gPressX - gViewportMinX;
        double my = gPressY - gViewportMinY;
        gPressX = mx;
        gPressY = my;
        if (!ViewportCursorInScene(mx, my)) return;   // 黑边不拾取

        // 1) 已有选中物：先抓中心小球（自由移动），再抓坐标轴
        if (HasSelection())
        {
            glm::mat4 vw, pj;
            CurrentViewProj(window, vw, pj);
            auto toPx = [&](const glm::vec3& w, float& sx, float& sy) -> bool
            {
                glm::vec4 clip = pj * vw * glm::vec4(w, 1.0f);
                if (clip.w <= 0.0001f) return false;
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                if (ndc.z < -1.0f || ndc.z > 1.0f) return false;
                sx = (ndc.x * 0.5f + 0.5f) * gSceneFBW;
                sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * gSceneFBH;
                return true;
            };
            float bsx, bsy;
            if (toPx(SelPos(), bsx, bsy))
            {
                float dx = (float)(mx * gViewportScaleX - gSceneFBX) - bsx;
                float dy = (float)(my * gViewportScaleY - gSceneFBY) - bsy;
                float tol = 20.0f * (float)gViewportScaleX;
                if (dx * dx + dy * dy <= tol * tol)
                {
                    gGrabAxis = 3;   // 3 = 中心球
            HWND hcap3 = glfwGetWin32Window(window);
            if (hcap3) SetCapture(hcap3);   // 窗口捕获：拖出窗口也持续跟踪
                    gDragMoveEnabled = true;
                    gDragStarted = true;
                    gGrabStartPos = SelPos();
                    gDragPlaneNormal = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
                        ? gCamera.Front : glm::vec3(0.0f, 1.0f, 0.0f);
                    glm::vec3 o, d;
                    ScreenToRay(window, vw, pj, mx, my, o, d);
                    float tt = 0.0f;
                    if (RayPlaneIntersect(o, d, SelPos(), gDragPlaneNormal, tt))
                        gGrabStartHit = o + d * tt;
                        gLastDragHit = gGrabStartHit;
                        gLastDragPos = gGrabStartPos;
                    return;
                }
            }
        }
        // 1) 已有选中物：优先抓坐标轴（抓轴 = 立即允许拖动）
        int axis = PickAxis(window, mx, my);
        if (axis >= 0)
        {
            gGrabAxis = axis;
                HWND hcapA = glfwGetWin32Window(window);
                if (hcapA) SetCapture(hcapA);
            gGrabStartPos = SelPos();
            gDragMoveEnabled = true;
            gDragStarted = true;
            gDragPlaneNormal = gCamera.Front;
            glm::mat4 view, proj;
            CurrentViewProj(window, view, proj);
            glm::vec3 origin, dir;
            ScreenToRay(window, view, proj, mx, my, origin, dir);
            float t = 0.0f;
            if (RayPlaneIntersect(origin, dir, SelPos(), gDragPlaneNormal, t))
                gGrabStartHit = origin + dir * t;
                gLastDragHit = gGrabStartHit;
                gLastDragPos = gGrabStartPos;
            return;
        }

        // 2) 射线拾取：灯用球、模型用世界 AABB
        glm::mat4 view, proj;
        CurrentViewProj(window, view, proj);
        glm::vec3 origin, dir;
        ScreenToRay(window, view, proj, mx, my, origin, dir);

        float bestT = 1e30f;
        int bestLight = -1, bestModel = -1;
        for (int i = 0; i < (int)gLights.size(); ++i)
        {
            float t = 0.0f;
            if (RaySphereIntersect(origin, dir, gLights[i].Position, 0.55f, t) && t < bestT)
            { bestT = t; bestLight = i; bestModel = -1; }
        }
        for (int i = 0; i < (int)gModels.size(); ++i)
        {
            glm::vec3 bmin, bmax;
            ModelWorldAABB(gModels[i], bmin, bmax);
            float t = 0.0f;
            if (RayAABB(origin, dir, bmin, bmax, t) && t < bestT)
            { bestT = t; bestLight = -1; bestModel = i; }
        }

        // 远距离小目标：射线未命中时做屏幕空间就近兜底
        if (bestLight < 0 && bestModel < 0)
        {
            float cx = (float)(mx * gViewportScaleX) - gSceneFBX;
            float cy = (float)(my * gViewportScaleY) - gSceneFBY;
            float tol = 18.0f * (float)gViewportScaleX;
            auto screenDist = [&](const glm::vec3& w) -> float
            {
                glm::vec4 clip = proj * view * glm::vec4(w, 1.0f);
                if (clip.w <= 0.0001f) return 1e9f;
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                if (ndc.z < -1.0f || ndc.z > 1.0f) return 1e9f;
                float sx = (ndc.x * 0.5f + 0.5f) * gSceneFBW;
                float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * gSceneFBH;
                float dx = sx - cx;
                float dy = sy - cy;
                return dx * dx + dy * dy;
            };
            float bestD = 1e9f;
            for (int i = 0; i < (int)gLights.size(); ++i)
            {
                float d = screenDist(gLights[i].Position);
                if (d < bestD) { bestD = d; bestLight = i; bestModel = -1; }
            }
            for (int i = 0; i < (int)gModels.size(); ++i)
            {
                float d = screenDist(gModels[i].Pos);
                if (d < bestD) { bestD = d; bestLight = -1; bestModel = i; }
            }
            if (bestD > tol * tol) { bestLight = -1; bestModel = -1; }
        }
        if (bestLight >= 0) SelectLight(bestLight);
        else if (bestModel >= 0) SelectModel(bestModel);
        else { ClearSelection(); return; }   // 点在所有选中框之外 => 取消选中

        // 点物体只负责选中/取消；移动必须拖中心小球或坐标轴
    }
    else if (action == GLFW_RELEASE)
    {
        gLeftDown = false;
        HWND hrel = glfwGetWin32Window(window);
        if (hrel) ReleaseCapture();
        gGrabAxis = -1;
        gDragStarted = false;
        gDragMoveEnabled = false;
    }
}

void CursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    gLastInputTime = glfwGetTime();
    bool curInViewport = (xpos >= gViewportMinX && ypos >= gViewportMinY &&
                          xpos < gViewportMinX + gViewportW && ypos < gViewportMinY + gViewportH);
    if (!curInViewport && !gRightMouseDown && !gLeftDown)
    {
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;
        return;
    }
    double lx = xpos - gViewportMinX;
    double ly = ypos - gViewportMinY;
    // 轴拖动：只沿该轴，增量跟随
    if (gLeftDown && gGrabAxis >= 0 && gGrabAxis <= 2 && HasSelection())
    {
        glm::mat4 view, proj;
        CurrentViewProj(window, view, proj);
        glm::vec3 origin, dir;
        DragRay(window, view, proj, lx, ly, origin, dir);
        float t = 0.0f;
        if (RayPlaneIntersect(origin, dir, gLastDragPos, gCamera.Front, t))
        {
            glm::vec3 hit = origin + dir * t;
            glm::vec3 axis = AxisDirWorld(gGrabAxis);
            float delta = glm::dot(hit - gLastDragHit, axis);
            glm::vec3 np = gLastDragPos + axis * delta;
            gLastDragHit = hit;
            gLastDragPos = np;
            SetSelPos(np);
        }
        return;
    }

    // 中心球：自由移动，增量跟随（快速拖动也不会脱手）
    if (gLeftDown && gGrabAxis == 3 && HasSelection() && gDragMoveEnabled)
    {
        glm::mat4 view, proj;
        CurrentViewProj(window, view, proj);
        glm::vec3 origin, dir;
        DragRay(window, view, proj, lx, ly, origin, dir);
        float t = 0.0f;
        if (RayPlaneIntersect(origin, dir, gLastDragPos, gDragPlaneNormal, t))
        {
            glm::vec3 hit = origin + dir * t;
            glm::vec3 offset = hit - gLastDragHit;
            offset -= gDragPlaneNormal * glm::dot(offset, gDragPlaneNormal);
            glm::vec3 np = gLastDragPos + offset;
            if (gSelLight >= 0 && np.y < 0.2f) np.y = 0.2f;
            gLastDragHit = hit;
            gLastDragPos = np;
            SetSelPos(np);
        }
        return;
    }
    if (!gRightMouseDown) return;
    if (gFirstMouse)
    {
        gLastX = xpos;
        gLastY = ypos;
        gFirstMouse = false;
    }
    double mdx = xpos - gLastX;
    double mdy = ypos - gLastY;
    gLastX = xpos;
    gLastY = ypos;
    gCamera.Rotate((float)mdx, (float)mdy);
}

void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    if (ImGui::GetCurrentContext())
        ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);

    double scx, scy;
    glfwGetCursorPos(window, &scx, &scy);
    if (scx < gViewportMinX || scy < gViewportMinY ||
        scx >= gViewportMinX + gViewportW || scy >= gViewportMinY + gViewportH)
        return;
    gFov -= (float)yoffset * 3.0f;
    if (gFov < 25.0f) gFov = 25.0f;
    if (gFov > 95.0f) gFov = 95.0f;
}

void DropCallback(GLFWwindow* window, int count, const char** paths)
{
    if (count <= 0 || paths == nullptr) return;
    std::string path = paths[0];
    std::string lower = path;
    for (auto& ch : lower) ch = (char)tolower(ch);
    if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".obj")
    {
        std::cout << "[拖拽] 只支持 .obj：" << path << std::endl;
        return;
    }
    int idx = AddModelFile(path);
    if (idx >= 0) SelectModel(idx);
}

// ---------- 几何生成 ----------
std::vector<float> MakeCubeData()
{
    std::vector<float> data;
    auto pushQuad = [&](const glm::vec3& n,
                        const glm::vec3& p0, const glm::vec3& p1,
                        const glm::vec3& p2, const glm::vec3& p3)
    {
        auto push = [&](const glm::vec3& p)
        {
            data.push_back(p.x); data.push_back(p.y); data.push_back(p.z);
            data.push_back(n.x); data.push_back(n.y); data.push_back(n.z);
        };
        push(p0); push(p1); push(p3);
        push(p0); push(p3); push(p2);
    };
    for (int axis = 0; axis < 3; ++axis)
        for (int sign = -1; sign <= 1; sign += 2)
        {
            glm::vec3 n(0.0f);
            n[axis] = (float)sign;
            glm::vec3 corners[4];
            int c = 0;
            for (int i = -1; i <= 1; i += 2)
                for (int j = -1; j <= 1; j += 2)
                {
                    glm::vec3 v(0.0f);
                    v[axis] = (float)sign * 0.5f;
                    int a = (axis + 1) % 3;
                    int b = (axis + 2) % 3;
                    v[a] = (float)i * 0.5f;
                    v[b] = (float)j * 0.5f;
                    corners[c++] = v;
                }
            pushQuad(n, corners[0], corners[1], corners[2], corners[3]);
        }
    return data;
}

std::vector<float> MakeSphereData(int segments, int rings)
{
    const float PI = 3.14159265f;
    std::vector<float> data;
    auto push = [&](const glm::vec3& p)
    {
        data.push_back(p.x); data.push_back(p.y); data.push_back(p.z);
    };
    for (int j = 0; j < rings; ++j)
    {
        float v0 = (float)j / rings * PI;
        float v1 = (float)(j + 1) / rings * PI;
        for (int i = 0; i < segments; ++i)
        {
            float u0 = (float)i / segments * 2.0f * PI;
            float u1 = (float)((i + 1) % segments) / segments * 2.0f * PI;
            glm::vec3 a(sinf(v0) * cosf(u0), cosf(v0), sinf(v0) * sinf(u0));
            glm::vec3 b(sinf(v0) * cosf(u1), cosf(v0), sinf(v0) * sinf(u1));
            glm::vec3 c(sinf(v1) * cosf(u0), cosf(v1), sinf(v1) * sinf(u0));
            glm::vec3 d(sinf(v1) * cosf(u1), cosf(v1), sinf(v1) * sinf(u1));
            push(a); push(d); push(b);
            push(a); push(c); push(d);
        }
    }
    return data;
}

void MakeGridData(std::vector<float>& data)
{
    auto line = [&](float x0, float y0, float z0,
                    float x1, float y1, float z1,
                    float r, float g, float b)
    {
        data.push_back(x0); data.push_back(y0); data.push_back(z0);
        data.push_back(r);  data.push_back(g);  data.push_back(b);
        data.push_back(x1); data.push_back(y1); data.push_back(z1);
        data.push_back(r);  data.push_back(g);  data.push_back(b);
    };
    const float half = 12.0f;
    for (int i = -12; i <= 12; ++i)
    {
        bool major = (i % 5 == 0);
        float c = major ? 0.55f : 0.28f;
        float x = (float)i;
        line(x, 0.0f, -half, x, 0.0f, half, c, c, c);
        line(-half, 0.0f, x, half, 0.0f, x, c, c, c);
    }
    line(0, 0, 0, 3, 0, 0, 1.0f, 0.2f, 0.2f);
    line(0, 0, 0, 0, 3, 0, 0.2f, 1.0f, 0.2f);
    line(0, 0, 0, 0, 0, 3, 0.2f, 0.3f, 1.0f);
}

std::vector<float> MakeAxesData()
{
    std::vector<float> data;
    auto line = [&](float x0, float y0, float z0,
                    float x1, float y1, float z1,
                    float r, float g, float b)
    {
        data.push_back(x0); data.push_back(y0); data.push_back(z0);
        data.push_back(r);  data.push_back(g);  data.push_back(b);
        data.push_back(x1); data.push_back(y1); data.push_back(z1);
        data.push_back(r);  data.push_back(g);  data.push_back(b);
    };
    line(0, 0, 0, 1, 0, 0, 1.0f, 0.25f, 0.25f);
    line(0, 0, 0, 0, 1, 0, 0.25f, 1.0f, 0.25f);
    line(0, 0, 0, 0, 0, 1, 0.30f, 0.45f, 1.0f);
    return data;
}

// 灯头箭头（局部指向 -Y）
std::vector<float> MakeArrowData()
{
    std::vector<float> data;
    auto line = [&](float x0, float y0, float z0, float x1, float y1, float z1)
    {
        for (int k = 0; k < 2; ++k)
        {
            data.push_back(k == 0 ? x0 : x1);
            data.push_back(k == 0 ? y0 : y1);
            data.push_back(k == 0 ? z0 : z1);
            data.push_back(1.0f); data.push_back(0.9f); data.push_back(0.4f);
        }
    };
    line(0, 0, 0, 0, -1.0f, 0);
    line(0, -1.0f, 0, -0.14f, -0.72f, 0);
    line(0, -1.0f, 0,  0.14f, -0.72f, 0);
    return data;
}

// 聚光灯外锥线框：apex 在原点，指向 -Y，底圆半径 1（距离 1 处）
std::vector<float> MakeSpotConeData()
{
    std::vector<float> data;
    auto add = [&](float x0, float y0, float z0, float x1, float y1, float z1)
    {
        for (int k = 0; k < 2; ++k)
        {
            data.push_back(k == 0 ? x0 : x1);
            data.push_back(k == 0 ? y0 : y1);
            data.push_back(k == 0 ? z0 : z1);
            data.push_back(0.55f); data.push_back(0.85f); data.push_back(1.0f);
        }
    };
    const int seg = 10;
    for (int i = 0; i < seg; ++i)
    {
        float a0 = (float)i / seg * 2.0f * 3.14159265f;
        float a1 = (float)((i + 1) % seg) / seg * 2.0f * 3.14159265f;
        float cx0 = cosf(a0), cz0 = sinf(a0);
        float cx1 = cosf(a1), cz1 = sinf(a1);
        add(0, 0, 0, cx0, -1.0f, cz0);          // 伞骨
        add(cx0, -1.0f, cz0, cx1, -1.0f, cz1);  // 底圈
    }
    return data;
}
glm::mat3 AlignToDirection(const glm::vec3& dir)
{
    glm::vec3 d = glm::normalize(dir);
    glm::vec3 from(0.0f, -1.0f, 0.0f);
    float dotv = glm::clamp(glm::dot(from, d), -1.0f, 1.0f);
    if (dotv > 0.999f) return glm::mat3(1.0f);
    if (dotv < -0.999f) return glm::mat3(glm::rotate(glm::mat4(1.0f), 3.14159265f, glm::vec3(1, 0, 0)));
    glm::vec3 axis = glm::cross(from, d);
    float ang = acosf(dotv);
    return glm::mat3(glm::rotate(glm::mat4(1.0f), ang, axis));
}

void DrawMesh(const Shader& shader, GLuint vao, GLsizei count,
              const glm::mat4& model, const glm::vec3& color)
{
    shader.SetMat4("uModel", model);
    glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(model)));
    shader.SetMat3("uNormalMat", normalMat);
    shader.SetVec3("uColor", color);
    shader.SetInt("uUseTex", 0);
    shader.SetInt("uSoftShade", 0);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, count);
    glBindVertexArray(0);
}

// 全屏 DockSpace 宿主：Unity 风格可停靠布局
void DockHostUI()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 220.0f), ImVec2(1e9f, 1e9f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoDocking;
    ImGui::Begin("MainDockSpaceHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(1);

    ImGuiID dockId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    // 首次运行：左侧停靠“场景编辑器”，右侧留白可继续拖入面板
    static bool layoutReady = false;
    if (!layoutReady)
    {
        layoutReady = true;
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);
        ImGuiID leftId, rightId;
        ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, 0.22f, &leftId, &rightId);
        ImGuiID toolId, hierId;
        ImGui::DockBuilderSplitNode(leftId, ImGuiDir_Up, 0.30f, &toolId, &hierId);
        ImGui::DockBuilderDockWindow("工具栏", toolId);
        ImGui::DockBuilderDockWindow("层级", hierId);
        ImGuiID centerId, rightCol;
        ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Right, 0.24f, &rightCol, &centerId);
        ImGui::DockBuilderDockWindow("属性", rightCol);
        ImGui::DockBuilderDockWindow("视口", centerId);
        ImGui::DockBuilderFinish(dockId);
    }
    ImGui::End();
}
// ---------- ImGui 侧边栏 ----------
void SidebarUI()
{
    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("工具栏", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::TextDisabled("FPS: %.0f", 1.0f / ImGui::GetIO().Framerate);
    ImGui::Separator();
    if (ImGui::RadioButton("世界坐标 World", !gLocalSpace)) gLocalSpace = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("本地坐标 Local", gLocalSpace)) gLocalSpace = true;
    ImGui::Checkbox("线框模式", &gWireframe);
    ImGui::SameLine();
    ImGui::Checkbox("显示网格", &gShowGrid);
    ImGui::SameLine();
    ImGui::Checkbox("天空盒", &gShowSky);
    ImGui::SameLine();
    ImGui::Checkbox("太阳跟随光源", &gSunFollowLight);
    ImGui::Checkbox("卡通着色", &gToon);
    ImGui::Checkbox("黑色描边", &gOutline);
    ImGui::Checkbox("后期调色", &gGrade);
    if (ImGui::Button("重置相机", ImVec2(-1, 0)))
    {
        gCamera = Camera();
        gCamera.UpdateVectors();
        gFov = 50.0f;
    }
    ImGui::SeparatorText("风格化参数");
    ImGui::ColorEdit3("暗部色温", &gShadowTint.x);
    ImGui::SliderFloat("色温强度", &gShadowAmt, 0.0f, 1.0f);
    ImGui::SliderFloat("亮部断点", &gBandHi, 0.5f, 1.0f);
    ImGui::SliderFloat("中间断点", &gBandMid, 0.1f, 0.6f);
    ImGui::SliderFloat("暗部断点", &gBandLo, 0.0f, 0.3f);
    ImGui::SliderFloat("边缘光", &gRimAmt, 0.0f, 1.0f);
    ImGui::SliderFloat("脸部补光", &gFaceFill, 0.0f, 1.0f);
    ImGui::SliderFloat("边缘光", &gRimAmt, 0.0f, 1.0f);
    ImGui::SliderFloat("脸部补光", &gFaceFill, 0.0f, 1.0f);
    ImGui::SliderFloat("饱和度", &gSatAmt, 0.0f, 1.2f);
    ImGui::SliderFloat("泛光", &gBloomAmt, 0.0f, 0.6f);
    ImGui::SliderFloat("颗粒", &gGrainAmt, 0.0f, 0.12f);
    ImGui::SeparatorText("动作");
    if (ImGui::Button("添加模型...", ImVec2(-1, 0)))
    {
        std::string path;
        if (PickObjFile(path))
        {
            int idx = AddModelFile(path);
            if (idx >= 0) SelectModel(idx);
        }
    }
    if (ImGui::Button("添加圆环", ImVec2(-1, 0))) { int idx = AddTorus(); if (idx >= 0) SelectModel(idx); }
    if (ImGui::Button("添加灯光", ImVec2(-1, 0))) { int idx = AddLight(); if (idx >= 0) SelectLight(idx); }
    if (ImGui::Button("添加平行光", ImVec2(-1, 0))) { int idx = AddLight(1); if (idx >= 0) SelectLight(idx); }
    if (ImGui::Button("添加聚光灯", ImVec2(-1, 0))) { int idx = AddLight(2); if (idx >= 0) SelectLight(idx); }
    if (ImGui::Button("删除选中", ImVec2(-1, 0))) DeleteSelected();
    ImGui::End();
}

// 层级面板：灯光 + 模型列表
void HierarchyUI()
{
    ImGui::Begin("层级", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (ImGui::CollapsingHeader("灯光", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (gLights.empty()) ImGui::TextDisabled("（无）");
        for (int i = 0; i < (int)gLights.size(); ++i)
        {
            char label[64];
            snprintf(label, sizeof(label), "灯 %d%s", i + 1, gLights[i].Selected ? "  [选中]" : "");
            if (ImGui::Selectable(label, gLights[i].Selected))
            {
                if (!gLights[i].Selected) SelectLight(i);
                else ClearSelection();
            }
        }
    }
    if (ImGui::CollapsingHeader("模型", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (gModels.empty()) ImGui::TextDisabled("（无）");
        for (int i = 0; i < (int)gModels.size(); ++i)
        {
            char label[128];
            snprintf(label, sizeof(label), "%s%s", gModels[i].Name.c_str(),
                     gModels[i].Selected ? "  [选中]" : "");
            if (ImGui::Selectable(label, gModels[i].Selected))
            {
                if (!gModels[i].Selected) SelectModel(i);
                else ClearSelection();
            }
        }
    }
    ImGui::End();
}

// 属性面板：选中对象的参数
void InspectorUI()
{
    ImGui::Begin("属性", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (gSelLight >= 0 && gSelLight < (int)gLights.size())
    {
        SceneLight& l = gLights[gSelLight];
        ImGui::Text("光源 #%d", gSelLight + 1);
        ImGui::Separator();
        float p[3] = { l.Position.x, l.Position.y, l.Position.z };
        if (ImGui::DragFloat3("位置", p, 0.1f))
            l.Position = glm::vec3(p[0], p[1], p[2]);
        float c[3] = { l.Color.x, l.Color.y, l.Color.z };
        if (ImGui::ColorEdit3("颜色", c))
            l.Color = glm::vec3(c[0], c[1], c[2]);
        ImGui::SliderFloat("强度", &l.Intensity, 0.1f, 10.0f);
        const char* lightTypes[] = { "点光源", "平行光", "聚光灯" };
        int lt = l.Type;
        if (ImGui::Combo("类型", &lt, lightTypes, 3)) l.Type = lt;
        if (l.Type >= 1)
        {
            float d[3] = { l.Direction.x, l.Direction.y, l.Direction.z };
            if (ImGui::DragFloat3("方向", d, 0.05f))
            {
                glm::vec3 nd(d[0], d[1], d[2]);
                if (glm::length(nd) > 0.001f) l.Direction = glm::normalize(nd);
            }
        }
        if (l.Type == 2)
        {
            ImGui::SliderFloat("内锥角", &l.InnerDeg, 1.0f, 89.0f);
            ImGui::SliderFloat("外锥角", &l.OuterDeg, 2.0f, 90.0f);
            if (l.OuterDeg < l.InnerDeg) l.OuterDeg = l.InnerDeg;
        }
    }
    else if (gSelModel >= 0 && gSelModel < (int)gModels.size())
    {
        SceneModel& m = gModels[gSelModel];
        ImGui::Text("模型: %s", m.Name.c_str());
        ImGui::Separator();
        float p[3] = { m.Pos.x, m.Pos.y, m.Pos.z };
        if (ImGui::DragFloat3("位置", p, 0.1f))
            m.Pos = glm::vec3(p[0], p[1], p[2]);
        ImGui::DragFloat("旋转 Y", &m.Yaw, 1.0f, -360.0f, 360.0f);
        float c[3] = { m.Color.x, m.Color.y, m.Color.z };
        if (ImGui::ColorEdit3("颜色", c))
            m.Color = glm::vec3(c[0], c[1], c[2]);
        if (ImGui::Button("删除此模型"))
        {
            DestroyMesh(m.Vao, m.Vbo);
            gModels.erase(gModels.begin() + gSelModel);
            ClearSelection();
        }
    }
    else
    {
        ImGui::TextDisabled("未选中任何对象");
        ImGui::TextWrapped("在视口里点击物体，或在左侧“层级”列表中选择。");
    }
    ImGui::End();
}

// 重建离屏渲染目标
void RecreateSceneTarget(int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if (gSceneColorTex) glDeleteTextures(1, &gSceneColorTex);
    if (gSceneDepthTex) glDeleteTextures(1, &gSceneDepthTex);
    if (gSceneNormalTex) glDeleteTextures(1, &gSceneNormalTex);
    if (gPostTex) glDeleteTextures(1, &gPostTex);
    if (gPostFbo) glDeleteFramebuffers(1, &gPostFbo);
    if (gSceneDepthTex) glDeleteTextures(1, &gSceneDepthTex);
    if (gSceneNormalTex) glDeleteTextures(1, &gSceneNormalTex);
    if (gSceneDepthRbo) glDeleteRenderbuffers(1, &gSceneDepthRbo);
    if (gSceneFbo) glDeleteFramebuffers(1, &gSceneFbo);
    if (gPostTex) glDeleteTextures(1, &gPostTex);
    if (gPostFbo) glDeleteFramebuffers(1, &gPostFbo);
    gFboW = w; gFboH = h;

    // 场景颜色
    glGenTextures(1, &gSceneColorTex);
    glBindTexture(GL_TEXTURE_2D, gSceneColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 场景深度（纹理，供描边采样）
    glGenTextures(1, &gSceneDepthTex);
    glBindTexture(GL_TEXTURE_2D, gSceneDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 法线纹理（视空间法线打包到 0..1）
    glGenTextures(1, &gSceneNormalTex);
    glBindTexture(GL_TEXTURE_2D, gSceneNormalTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // 后处理颜色
    glGenTextures(1, &gPostTex);
    glBindTexture(GL_TEXTURE_2D, gPostTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 场景 FBO：颜色 + 深度纹理
    glGenFramebuffers(1, &gSceneFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, gSceneFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gSceneColorTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gSceneDepthTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gSceneNormalTex, 0);

    // 后处理 FBO
    glGenFramebuffers(1, &gPostFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, gPostFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gPostTex, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// 由面板逻辑尺寸 + DPI 缩放，计算视口的 framebuffer 像素尺寸
void UpdateViewportMetrics(GLFWwindow* window)
{
    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowContentScale(window, &sx, &sy);
    gViewportScaleX = sx > 0.01f ? sx : 1.0f;
    gViewportScaleY = sy > 0.01f ? sy : 1.0f;
    gViewportFBW = (gViewportW > 0) ? (int)(gViewportW * gViewportScaleX) : 0;
    gViewportFBH = (gViewportH > 0) ? (int)(gViewportH * gViewportScaleY) : 0;
    if (gViewportW > 0 && gViewportFBW < 8) gViewportFBW = 8;
    if (gViewportH > 0 && gViewportFBH < 8) gViewportFBH = 8;

    // 16:9 场景在 FBO 内的 letterbox 矩形
    if (gViewportFBW > 0 && gViewportFBH > 0)
    {
        float panelAspect = (float)gViewportFBW / (float)gViewportFBH;
        if (panelAspect > kSceneAspect)
        {
            gSceneFBH = gViewportFBH;
            gSceneFBW = (int)(gViewportFBH * kSceneAspect);
            gSceneFBX = (gViewportFBW - gSceneFBW) / 2;
            gSceneFBY = 0;
        }
        else
        {
            gSceneFBW = gViewportFBW;
            gSceneFBH = (int)(gViewportFBW / kSceneAspect);
            gSceneFBX = 0;
            gSceneFBY = (gViewportFBH - gSceneFBH) / 2;
        }
    }
}

// 视口内逻辑坐标是否落在 16:9 场景区（黑边返回 false）
bool ViewportCursorInScene(double lx, double ly)
{
    if (gSceneFBW <= 0 || gSceneFBH <= 0) return false;
    double fx = lx * gViewportScaleX;
    double fy = ly * gViewportScaleY;
    return fx >= gSceneFBX && fy >= gSceneFBY &&
           fx < gSceneFBX + gSceneFBW && fy < gSceneFBY + gSceneFBH;
}
// 视口面板：显示离屏场景纹理，同时记录面板矩形供拾取/拖拽使用
void ViewportUI()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 220.0f), ImVec2(1e9f, 1e9f));
    ImGui::Begin("视口", nullptr,
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 pos  = ImGui::GetWindowPos();
    ImVec2 min  = ImGui::GetWindowContentRegionMin();
    ImVec2 max  = ImGui::GetWindowContentRegionMax();

    gViewportMinX = pos.x + min.x;
    gViewportMinY = pos.y + min.y;
    gViewportW = (int)(max.x - min.x);
    gViewportH = (int)(max.y - min.y);
    if (gViewportW < 8) gViewportW = 8;
    if (gViewportH < 8) gViewportH = 8;

    GLuint displayTex = gPostTex ? gPostTex : gSceneColorTex;
    if (displayTex)
        ImGui::Image((ImTextureID)(intptr_t)displayTex,
                     ImVec2((float)gViewportW, (float)gViewportH),
                     ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));  // 翻转 Y
    else
        ImGui::TextDisabled("(无渲染目标)");
    ImGui::End();
    ImGui::PopStyleVar();
}
// ---------- 主函数 ----------
int main()
{
    SetConsoleOutputCP(CP_UTF8);   // 控制台 UTF-8 输出，避免中文乱码
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(1400, 820, "OpenGL Scene Editor", nullptr, nullptr);
    if (!window)
    {
        std::cout << "创建窗口失败" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    DisableImeForWindow(window);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        std::cout << "初始化 GLEW 失败" << std::endl;
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glfwSwapInterval(1);

    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetDropCallback(window, DropCallback);
    glfwSetCharCallback(window, CharCallback);
    glfwSetKeyCallback(window, KeyCallback);

    gCamera.UpdateVectors();

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
    ImGui_ImplGlfw_InitForOpenGL(window, false);
    ImGui_ImplOpenGL3_Init("#version 330");
    gImGuiReady = true;

    std::cout << "======== OpenGL 场景编辑器 ========\n";
    std::cout << "全部编辑操作可在左侧 ImGui 面板完成；右键拖动转视角/WASD 移动\n";
    std::cout << "====================================\n";

    Shader worldShader(worldVertSrc, worldFragSrc);
    Shader lineShader(lineVertSrc, lineFragSrc);
    Shader skyShader(skyVertSrc, skyFragSrc);
    Shader flatShader(flatVertSrc, flatFragSrc);
    Shader postShader(postVertSrc, postFragSrc);
    Shader normalShader(normalVertSrc, normalFragSrc);
    glGenVertexArrays(1, &gPostVao);

    worldShader.Use();
    worldShader.SetFloat("uShininess", 48.0f);

    std::vector<float> cubeData = MakeCubeData();
    GLuint cubeVao = 0, cubeVbo = 0;
    UploadMesh(cubeData, cubeVao, cubeVbo);
    GLsizei cubeCount = (GLsizei)(cubeData.size() / 6);

    std::vector<float> sphereData = MakeSphereData(24, 12);
    GLuint sphereVao = 0, sphereVbo = 0;
    UploadMesh(sphereData, sphereVao, sphereVbo);
    GLsizei sphereCount = (GLsizei)(sphereData.size() / 6);

    std::vector<float> gridData;
    MakeGridData(gridData);
    GLuint gridVao = 0, gridVbo = 0;
    UploadMesh(gridData, gridVao, gridVbo);
    GLsizei gridCount = (GLsizei)(gridData.size() / 6);

    std::vector<float> axesData = MakeAxesData();
    GLuint axesVao = 0, axesVbo = 0;
    UploadMesh(axesData, axesVao, axesVbo);
    GLsizei axesCount = (GLsizei)(axesData.size() / 6);
    std::vector<float> arrowData = MakeArrowData();
    GLuint arrowVao = 0, arrowVbo = 0;
    UploadMesh(arrowData, arrowVao, arrowVbo);
    GLsizei arrowCount = (GLsizei)(arrowData.size() / 6);
    std::vector<float> coneData = MakeSpotConeData();
    GLuint coneVao = 0, coneVbo = 0;
    UploadMesh(coneData, coneVao, coneVbo);
    GLsizei coneCount = (GLsizei)(coneData.size() / 6);

    std::vector<float> boxEdgesData = MakeBoxEdgesData();
    GLuint boxEdgesVao = 0, boxEdgesVbo = 0;
    UploadMesh(boxEdgesData, boxEdgesVao, boxEdgesVbo);
    GLsizei boxEdgesCount = (GLsizei)(boxEdgesData.size() / 6);

    struct BoxPlacement { glm::vec3 Pos, Scale; glm::vec3 Color; };
    std::vector<BoxPlacement> boxes;
    for (int i = 0; i < 6; ++i)
    {
        float angle = i / 6.0f * 2.0f * 3.14159265f;
        float scaleY = 0.8f + (i % 3) * 0.35f;
        boxes.push_back({
            glm::vec3(4.5f * cosf(angle), scaleY * 0.5f, 4.5f * sinf(angle)),
            glm::vec3(1.0f, scaleY, 1.0f),
            glm::vec3((i & 1) ? 0.95f : 0.25f, (i & 2) ? 0.45f : 0.85f, (i == 3) ? 0.95f : 0.35f)
        });
    }

    gLights.push_back(SceneLight());
    AddTorus();

    float lastFrame = (float)glfwGetTime();
    int frameCount = 0;
    float statTimer = 0.0f;

    while (!glfwWindowShouldClose(window))
    {
        float now = (float)glfwGetTime();
        float dt = now - lastFrame;
        lastFrame = now;
        if (dt > 0.05f) dt = 0.05f;
        glfwPollEvents();   // 先处理事件，保证 ImGui 输入及时

        // Alt：强制呼出鼠标光标（右键转视角后光标卡住/丢失时使用）
        static bool prevAlt = false;
        bool nowAlt = (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) ||
                      (glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
        if (nowAlt && !prevAlt)
        {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            gRightMouseDown = false;
            gFirstMouse = true;
            std::cout << "[提示] 已呼出鼠标光标（Alt）" << std::endl;
        }
        prevAlt = nowAlt;

        // 防呆：隐藏光标状态连续 6 秒无任何输入动作则自动恢复
        if (gRightMouseDown && (now - gLastInputTime) > 6.0)
        {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            gRightMouseDown = false;
            gFirstMouse = true;
            std::cout << "[提示] 长时间无操作，已自动恢复鼠标光标" << std::endl;
        }

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        bool uiCaptureKeys = ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;
        if (!uiCaptureKeys)
        {
            if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS)
            {
                gCamera = Camera();
                gCamera.UpdateVectors();
                gFov = 50.0f;
            }

            static bool prevO = false;
            bool nowO = glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS;
            if (nowO && !prevO)
            {
                std::string path;
                if (PickObjFile(path))
                {
                    int idx = AddModelFile(path);
                    if (idx >= 0) SelectModel(idx);
                }
            }
            prevO = nowO;

            static bool prevT = false;
            bool nowT = glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS;
            if (nowT && !prevT) { int idx = AddTorus(); if (idx >= 0) SelectModel(idx); }
            prevT = nowT;

            static bool prevL = false;
            bool nowL = glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS;
            if (nowL && !prevL) { int idx = AddLight(); if (idx >= 0) SelectLight(idx); }
            prevL = nowL;

            static bool prevX = false;
            bool nowX = (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS ||
                         glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS);
            if (nowX && !prevX) DeleteSelected();
            prevX = nowX;

            static bool prevV = false;
            bool nowV = glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS;
            if (nowV && !prevV) gLocalSpace = !gLocalSpace;
            prevV = nowV;

            static bool prevF1 = false;
            bool nowF1 = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
            if (nowF1 && !prevF1) gWireframe = !gWireframe;
            prevF1 = nowF1;

            if (gSelModel >= 0 && gSelModel < (int)gModels.size())
            {
                float rot = 0.0f;
                if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) rot += 60.0f * dt;
                if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) rot -= 60.0f * dt;
                gModels[gSelModel].Yaw += rot;
            }

            int fwd = (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ? 1 : 0)
                    - (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ? 1 : 0);
            int str = (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ? 1 : 0)
                    - (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ? 1 : 0);
            int up  = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS ? 1 : 0)
                    - (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ? 1 : 0);
            bool boost = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
            gCamera.Move(fwd, str, up, dt, boost);
        }

        glm::mat4 view = gCamera.GetViewMatrix();
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
                float aspect = (gSceneFBH > 0) ? (float)gSceneFBW / (float)gSceneFBH : ((fbH > 0) ? (float)fbW / (float)fbH : 1.0f);
        glm::mat4 proj = glm::perspective(glm::radians(gFov), aspect, 0.05f, 500.0f);

        // 每帧先按视口面板尺寸刷新度量（含 16:9 letterbox 矩形）
        UpdateViewportMetrics(window);
        int targetW = (gViewportFBW > 0) ? gViewportFBW : fbW;
        int targetH = (gViewportFBH > 0) ? gViewportFBH : fbH;
        if (targetW != gFboW || targetH != gFboH) RecreateSceneTarget(targetW, targetH);
        glBindFramebuffer(GL_FRAMEBUFFER, gSceneFbo);
        glViewport(0, 0, gFboW, gFboH);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);          // 黑边
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 只把 16:9 场景画在中间区域（未就绪时退化为全幅）
        int sx = (gSceneFBW > 0) ? gSceneFBX : 0;
        int syTop = (gSceneFBW > 0) ? gSceneFBY : 0;
        int sw = (gSceneFBW > 0) ? gSceneFBW : gFboW;
        int sh = (gSceneFBH > 0) ? gSceneFBH : gFboH;
        int sy = gFboH - syTop - sh;
        glEnable(GL_SCISSOR_TEST);
        glViewport(sx, sy, sw, sh);
        glScissor(sx, sy, sw, sh);
        glClearColor(0.06f, 0.07f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);

        worldShader.Use();
        worldShader.SetMat4("uView", view);
        worldShader.SetMat4("uProj", proj);
        worldShader.SetVec3("uViewPos", gCamera.Position);
        worldShader.SetFloat("uToon", gToon ? 1.0f : 0.0f);
        worldShader.SetFloat("uBandHi", gBandHi);
        worldShader.SetFloat("uBandMid", gBandMid);
        worldShader.SetFloat("uBandLo", gBandLo);
        worldShader.SetVec3("uShadowTint", gShadowTint);
        worldShader.SetFloat("uShadowAmt", gShadowAmt);
        worldShader.SetVec3("uSpecColor", gSpecColor);
        worldShader.SetFloat("uRimAmt", gRimAmt);
        worldShader.SetFloat("uFaceFill", gFaceFill);
        worldShader.SetFloat("uRimAmt", gRimAmt);
        worldShader.SetFloat("uFaceFill", gFaceFill);

        int lightCount = (int)gLights.size();
        if (lightCount > kMaxLights) lightCount = kMaxLights;
        worldShader.SetInt("uLightCount", lightCount);
        if (lightCount > 0)
        {
            std::vector<glm::vec3> posArr, colArr;
            std::vector<float> typeArr, coneInArr, coneOutArr;
            std::vector<glm::vec3> dirArr;
            std::vector<float> intArr;
            for (int i = 0; i < lightCount; ++i)
            {
                posArr.push_back(gLights[i].Position);
                colArr.push_back(gLights[i].Color);
                intArr.push_back(gLights[i].Intensity);
                typeArr.push_back((float)gLights[i].Type);
                dirArr.push_back(gLights[i].Direction);
                float cin = cos(glm::radians(std::min(gLights[i].InnerDeg, gLights[i].OuterDeg)));
                float cout = cos(glm::radians(std::max(gLights[i].InnerDeg, gLights[i].OuterDeg)));
                coneInArr.push_back(cin);
                coneOutArr.push_back(cout);
            }
            worldShader.SetVec3Array("uLightPos", posArr.data(), lightCount);
            worldShader.SetVec3Array("uLightColor", colArr.data(), lightCount);
            worldShader.SetFloatArray("uLightIntensity", intArr.data(), lightCount);
            worldShader.SetFloatArray("uLightTypeF", typeArr.data(), lightCount);
            worldShader.SetVec3Array("uLightDir", dirArr.data(), lightCount);
            worldShader.SetFloatArray("uLightConeIn", coneInArr.data(), lightCount);
            worldShader.SetFloatArray("uLightConeOut", coneOutArr.data(), lightCount);
        }

        for (const BoxPlacement& box : boxes)
        {
            glm::mat4 model(1.0f);
            model = glm::translate(model, box.Pos);
            model = glm::scale(model, box.Scale);
            DrawMesh(worldShader, cubeVao, cubeCount, model, box.Color);
        }

        for (SceneModel& m : gModels)
        {
            if (!m.Valid) continue;
            glm::mat4 model(1.0f);
            model = glm::translate(model, m.Pos);
            model = glm::rotate(model, glm::radians(m.Yaw), glm::vec3(0, 1, 0));
            model = glm::scale(model, glm::vec3(m.Scale));
                DrawModelRanges(worldShader, m, model, m.Color);
        }

        flatShader.Use();
        for (int i = 0; i < (int)gLights.size(); ++i)
        {
            const SceneLight& l = gLights[i];
            glm::mat4 gizmo(1.0f);
            gizmo = glm::translate(gizmo, l.Position);
            float s = l.Selected ? 0.55f : 0.42f;
            gizmo = glm::scale(gizmo, glm::vec3(s));
            flatShader.SetMat4("uMvp", proj * view * gizmo);
            flatShader.SetVec3("uColor", l.Selected ? glm::vec3(1.0f, 0.9f, 0.35f) : l.Color);
            glBindVertexArray(sphereVao);
            glDrawArrays(GL_TRIANGLES, 0, sphereCount);
            glBindVertexArray(0);
        }

        if (HasSelection())
        {
            lineShader.Use();
            lineShader.SetMat4("uView", view);
            lineShader.SetMat4("uProj", proj);
            glm::mat4 g(1.0f);
            g = glm::translate(g, SelPos());
            if (gLocalSpace && gSelModel >= 0)
                g = glm::rotate(g, glm::radians(gModels[gSelModel].Yaw), glm::vec3(0, 1, 0));
            g = glm::scale(g, glm::vec3(SelGizmoLen()));
            lineShader.SetMat4("uModel", g);
            glBindVertexArray(axesVao);
            glDrawArrays(GL_LINES, 0, axesCount);
            glBindVertexArray(0);
        }

        // 平行光/聚光灯：方向箭头
        lineShader.Use();
        lineShader.SetMat4("uView", view);
        lineShader.SetMat4("uProj", proj);
        for (int i = 0; i < (int)gLights.size(); ++i)
        {
            const SceneLight& l = gLights[i];
            if (l.Type < 1) continue;
            glm::mat4 am(1.0f);
            am = glm::translate(am, l.Position);
            am = am * glm::mat4(AlignToDirection(l.Direction));
            am = glm::scale(am, glm::vec3(1.8f));
            lineShader.SetMat4("uModel", am);
            glBindVertexArray(arrowVao);
            glDrawArrays(GL_LINES, 0, arrowCount);
            glBindVertexArray(0);
            // 聚光灯：外锥可视化
            if (l.Type == 2)
            {
                float len = 1.8f;
                float tr = tanf(glm::radians(std::max(l.OuterDeg, 0.5f))) * len;
                glm::mat4 cm(1.0f);
                cm = glm::translate(cm, l.Position);
                cm = cm * glm::mat4(AlignToDirection(l.Direction));
                cm = glm::scale(cm, glm::vec3(tr, len, tr));
                lineShader.SetMat4("uModel", cm);
                glBindVertexArray(coneVao);
                glDrawArrays(GL_LINES, 0, coneCount);
                glBindVertexArray(0);
            }
            glBindVertexArray(0);
        }
        // 选中物体：中心小球 = 自由移动手柄
        if (HasSelection())
        {
            flatShader.Use();
            glm::mat4 ballM(1.0f);
            ballM = glm::translate(ballM, SelPos());
            ballM = glm::scale(ballM, glm::vec3(0.18f));
            flatShader.SetMat4("uMvp", proj * view * ballM);
            flatShader.SetVec3("uColor", glm::vec3(1.0f, 0.96f, 0.88f));
            glBindVertexArray(sphereVao);
            glDrawArrays(GL_TRIANGLES, 0, sphereCount);
            glBindVertexArray(0);
        }
        // 选中模型的橙色包围盒线框
        if (gSelModel >= 0 && gSelModel < (int)gModels.size())
        {
            SceneModel& sm = gModels[gSelModel];
            glm::vec3 size = sm.BoundsMax - sm.BoundsMin;
            glm::mat4 bm(1.0f);
            bm = glm::translate(bm, sm.Pos);
            bm = glm::rotate(bm, glm::radians(sm.Yaw), glm::vec3(0, 1, 0));
            bm = glm::scale(bm, size);
            lineShader.Use();
            lineShader.SetMat4("uView", view);
            lineShader.SetMat4("uProj", proj);
            lineShader.SetMat4("uModel", bm);
            glBindVertexArray(boxEdgesVao);
            glDrawArrays(GL_LINES, 0, boxEdgesCount);
            glBindVertexArray(0);
        }

        if (gShowGrid)
        {
            lineShader.Use();
            lineShader.SetMat4("uView", view);
            lineShader.SetMat4("uProj", proj);
            lineShader.SetMat4("uModel", glm::mat4(1.0f));
            glBindVertexArray(gridVao);
            glDrawArrays(GL_LINES, 0, gridCount);
            glBindVertexArray(0);
        }

        if (gShowSky)
        {
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            skyShader.Use();
            skyShader.SetMat4("uView", view);
            skyShader.SetMat4("uProj", proj);
            glm::vec3 sunDir(0.3f, 0.8f, 0.4f);   // 默认：太阳固定不动
            if (gSunFollowLight && !gLights.empty())
                sunDir = glm::length(gLights[0].Position) > 0.01f ? glm::normalize(gLights[0].Position) : sunDir;
            skyShader.SetVec3("uSunDir", sunDir);
            glBindVertexArray(cubeVao);
            glDrawArrays(GL_TRIANGLES, 0, cubeCount);
            glBindVertexArray(0);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
        }

        // ---- 法线通道：内部折痕描边用 ----
        if (gSceneNormalTex)
        {
            glDrawBuffer(GL_COLOR_ATTACHMENT1);
            glDepthFunc(GL_LEQUAL);
            glClearColor(0.5f, 0.5f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            normalShader.Use();
            normalShader.SetMat4("uView", view);
            normalShader.SetMat4("uProj", proj);
            for (const BoxPlacement& box : boxes)
            {
                glm::mat4 model(1.0f);
                model = glm::translate(model, box.Pos);
                model = glm::scale(model, box.Scale);
                DrawMesh(normalShader, cubeVao, cubeCount, model, box.Color);
            }
            for (SceneModel& m : gModels)
            {
                if (!m.Valid) continue;
                glm::mat4 model(1.0f);
                model = glm::translate(model, m.Pos);
                model = glm::rotate(model, glm::radians(m.Yaw), glm::vec3(0, 1, 0));
                model = glm::scale(model, glm::vec3(m.Scale));
                DrawMesh(normalShader, m.Vao, m.Count, model, m.Color);
            }
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glDepthFunc(GL_LESS);
        }
        // 屏幕空间描边后处理
        if (gPostFbo && gSceneColorTex && gSceneDepthTex)
        {
            glDisable(GL_DEPTH_TEST);
            glBindFramebuffer(GL_FRAMEBUFFER, gPostFbo);
            glViewport(0, 0, gFboW, gFboH);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            postShader.Use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gSceneColorTex);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, gSceneDepthTex);
            postShader.SetInt("uScene", 0);
            postShader.SetInt("uDepth", 1);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, gSceneNormalTex);
            postShader.SetInt("uNormal", 2);
            postShader.SetFloat("uOutlineOn", gOutline ? 1.0f : 0.0f);
            postShader.SetFloat("uGradeOn", gGrade ? 1.0f : 0.0f);
            postShader.SetFloat("uSatAmt", gSatAmt);
            postShader.SetFloat("uTime", (float)glfwGetTime());
            postShader.SetFloat("uBloomAmt", gBloomAmt);
            postShader.SetFloat("uGrainAmt", gGrainAmt);
            glBindVertexArray(gPostVao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glEnable(GL_DEPTH_TEST);
        }

        // 3D 场景已画进 FBO；切回默认缓冲渲染 ImGui
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.10f, 0.11f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // ---- ImGui 侧边栏 ----
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        DockHostUI();
        SidebarUI();
        ViewportUI();
        HierarchyUI();
        InspectorUI();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);

        frameCount++;
        statTimer += dt;
        if (statTimer >= 0.5f)
        {
            float fps = frameCount / statTimer;
            frameCount = 0;
            statTimer = 0.0f;
            std::string selInfo = "None";
            if (gSelModel >= 0) selInfo = "Model: " + gModels[gSelModel].Name;
            else if (gSelLight >= 0) selInfo = "Light #" + std::to_string(gSelLight + 1);
            char title[512];
            snprintf(title, sizeof(title),
                     "Scene Editor  |  Space: %s  |  Sel: %s  |  Lights: %d/%d  |  FPS: %.0f",
                     gLocalSpace ? "Local" : "World", selInfo.c_str(),
                     lightCount, kMaxLights, fps);
            glfwSetWindowTitle(window, title);
        }
    }

    // ---- 清理 ----
    if (gImGuiReady)
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    if (window)
    {
        HWND hwnd = glfwGetWin32Window(window);
        if (hwnd && g_SavedImeContext)
            ImmAssociateContext(hwnd, g_SavedImeContext);
    }
    glDeleteVertexArrays(1, &cubeVao);
    glDeleteBuffers(1, &cubeVbo);
    glDeleteVertexArrays(1, &sphereVao);
    glDeleteBuffers(1, &sphereVbo);
    glDeleteVertexArrays(1, &gridVao);
    glDeleteBuffers(1, &gridVbo);
glDeleteVertexArrays(1, &axesVao);
    if (gPostVao) glDeleteVertexArrays(1, &gPostVao);
    glDeleteBuffers(1, &axesVbo);
    glDeleteVertexArrays(1, &arrowVao);
    glDeleteBuffers(1, &arrowVbo);
    glDeleteVertexArrays(1, &coneVao);
    glDeleteBuffers(1, &coneVbo);
    glDeleteVertexArrays(1, &axesVao);
    if (gPostVao) glDeleteVertexArrays(1, &gPostVao);
    glDeleteBuffers(1, &axesVbo);
    glDeleteVertexArrays(1, &arrowVao);
    glDeleteBuffers(1, &arrowVbo);
    glDeleteVertexArrays(1, &coneVao);
    glDeleteBuffers(1, &coneVbo);
    glDeleteVertexArrays(1, &boxEdgesVao);
    glDeleteBuffers(1, &boxEdgesVbo);
    if (gSceneColorTex) glDeleteTextures(1, &gSceneColorTex);
    if (gSceneDepthTex) glDeleteTextures(1, &gSceneDepthTex);
    if (gSceneNormalTex) glDeleteTextures(1, &gSceneNormalTex);
    if (gPostTex) glDeleteTextures(1, &gPostTex);
    if (gPostFbo) glDeleteFramebuffers(1, &gPostFbo);
    if (gSceneDepthRbo) glDeleteRenderbuffers(1, &gSceneDepthRbo);
    if (gSceneFbo) glDeleteFramebuffers(1, &gSceneFbo);
    for (SceneModel& m : gModels) DestroyMesh(m.Vao, m.Vbo);

    glfwTerminate();
    return 0;
}
