#pragma once
// ============================================================
// Settings.h —— 渲染参数集中管理
// 把散落在全局的显示开关、风格化参数收进一个结构体。
// 纯数据，无 OpenGL 依赖，可直接被 UI 读写。
// ============================================================

#include <glm/glm.hpp>

struct RenderSettings
{
    // ---- 相机 / 视口 ----
    float Fov = 50.0f;              // 垂直视场角（度）
    bool  Wireframe = false;        // 线框模式

    // ---- 场景显示 ----
    bool  ShowGrid = true;          // 地面网格
    bool  ShowSky  = true;          // 天空盒
    bool  SunFollowLight = false;   // 天空太阳是否跟随第一盏灯

    // ---- 风格化开关 ----
    bool  Toon    = true;           // 卡通 / 赛璐璐着色
    bool  Outline = true;           // 屏幕空间黑色描边
    bool  Grade   = true;           // 后期调色（降饱和 / 抬黑位 / 暗角）

    // ---- 风格化参数 ----
    glm::vec3 ShadowTint = glm::vec3(0.42f, 0.38f, 0.62f);   // 暗部色温（偏紫）
    float ShadowAmt = 0.35f;
    float BandHi  = 0.80f;
    float BandMid = 0.30f;
    float BandLo  = 0.05f;
    glm::vec3 SpecColor = glm::vec3(1.0f, 0.98f, 0.92f);
    float RimAmt   = 0.60f;
    float FaceFill = 0.18f;
    float SatAmt   = 0.80f;
    float BloomAmt = 0.12f;
    float GrainAmt = 0.03f;
};
