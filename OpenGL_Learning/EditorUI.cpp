#include "EditorUI.h"

#include <cstdio>

#include <imgui.h>
#include <imgui_internal.h>

#include "Camera.h"
#include "Platform.h"
#include "Renderer.h"
#include "Scene.h"
#include "Settings.h"
#include "Viewport.h"

void EditorUI::Draw()
{
    DockHostUI();
    SidebarUI();
    ViewportUI();
    HierarchyUI();
    InspectorUI();
}

// 全屏 DockSpace 宿主：Unity 风格可停靠布局
void EditorUI::DockHostUI()
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

// 工具栏：坐标空间、显示开关、风格化参数、动作
void EditorUI::SidebarUI()
{
    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("工具栏", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::TextDisabled("FPS: %.0f", 1.0f / ImGui::GetIO().Framerate);
    ImGui::Separator();
    if (ImGui::RadioButton("世界坐标 World", !m_scene.localSpace)) m_scene.localSpace = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("本地坐标 Local", m_scene.localSpace)) m_scene.localSpace = true;
    ImGui::Checkbox("线框模式", &m_settings.Wireframe);
    ImGui::SameLine();
    ImGui::Checkbox("显示网格", &m_settings.ShowGrid);
    ImGui::SameLine();
    ImGui::Checkbox("天空盒", &m_settings.ShowSky);
    ImGui::SameLine();
    ImGui::Checkbox("太阳跟随光源", &m_settings.SunFollowLight);
    ImGui::Checkbox("卡通着色", &m_settings.Toon);
    ImGui::Checkbox("黑色描边", &m_settings.Outline);
    ImGui::Checkbox("后期调色", &m_settings.Grade);
    if (ImGui::Button("重置相机", ImVec2(-1, 0)))
    {
        m_camera = Camera();
        m_camera.UpdateVectors();
        m_settings.Fov = 50.0f;
    }
    ImGui::SeparatorText("风格化参数");
    ImGui::ColorEdit3("暗部色温", &m_settings.ShadowTint.x);
    ImGui::SliderFloat("色温强度", &m_settings.ShadowAmt, 0.0f, 1.0f);
    ImGui::SliderFloat("亮部断点", &m_settings.BandHi, 0.5f, 1.0f);
    ImGui::SliderFloat("中间断点", &m_settings.BandMid, 0.1f, 0.6f);
    ImGui::SliderFloat("暗部断点", &m_settings.BandLo, 0.0f, 0.3f);
    ImGui::SliderFloat("边缘光", &m_settings.RimAmt, 0.0f, 1.0f);
    ImGui::SliderFloat("脸部补光", &m_settings.FaceFill, 0.0f, 1.0f);
    ImGui::SliderFloat("饱和度", &m_settings.SatAmt, 0.0f, 1.2f);
    ImGui::SliderFloat("泛光", &m_settings.BloomAmt, 0.0f, 0.6f);
    ImGui::SliderFloat("颗粒", &m_settings.GrainAmt, 0.0f, 0.12f);
    ImGui::SeparatorText("动作");
    if (ImGui::Button("添加模型...", ImVec2(-1, 0)))
    {
        std::string path;
        if (Platform::PickObjFile(path))
        {
            int idx = m_scene.AddModelFile(path);
            if (idx >= 0) m_scene.SelectModel(idx);
        }
    }
    if (ImGui::Button("添加圆环", ImVec2(-1, 0))) { int idx = m_scene.AddTorus(); if (idx >= 0) m_scene.SelectModel(idx); }
    if (ImGui::Button("添加灯光", ImVec2(-1, 0))) { int idx = m_scene.AddLight(0, m_camera.Position, m_camera.Front); if (idx >= 0) m_scene.SelectLight(idx); }
    if (ImGui::Button("添加平行光", ImVec2(-1, 0))) { int idx = m_scene.AddLight(1, m_camera.Position, m_camera.Front); if (idx >= 0) m_scene.SelectLight(idx); }
    if (ImGui::Button("添加聚光灯", ImVec2(-1, 0))) { int idx = m_scene.AddLight(2, m_camera.Position, m_camera.Front); if (idx >= 0) m_scene.SelectLight(idx); }
    if (ImGui::Button("删除选中", ImVec2(-1, 0))) m_scene.DeleteSelected();
    ImGui::End();
}

// 层级面板：灯光 + 模型列表
void EditorUI::HierarchyUI()
{
    ImGui::Begin("层级", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (ImGui::CollapsingHeader("灯光", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (m_scene.lights.empty()) ImGui::TextDisabled("（无）");
        for (int i = 0; i < (int)m_scene.lights.size(); ++i)
        {
            char label[64];
            snprintf(label, sizeof(label), "灯 %d%s", i + 1, m_scene.lights[i].Selected ? "  [选中]" : "");
            if (ImGui::Selectable(label, m_scene.lights[i].Selected))
            {
                if (!m_scene.lights[i].Selected) m_scene.SelectLight(i);
                else m_scene.ClearSelection();
            }
        }
    }
    if (ImGui::CollapsingHeader("模型", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (m_scene.models.empty()) ImGui::TextDisabled("（无）");
        for (int i = 0; i < (int)m_scene.models.size(); ++i)
        {
            char label[128];
            snprintf(label, sizeof(label), "%s%s", m_scene.models[i].Name.c_str(),
                     m_scene.models[i].Selected ? "  [选中]" : "");
            if (ImGui::Selectable(label, m_scene.models[i].Selected))
            {
                if (!m_scene.models[i].Selected) m_scene.SelectModel(i);
                else m_scene.ClearSelection();
            }
        }
    }
    ImGui::End();
}

// 属性面板：选中对象的参数
void EditorUI::InspectorUI()
{
    ImGui::Begin("属性", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysVerticalScrollbar);
    int selLight = m_scene.SelLight();
    int selModel = m_scene.SelModel();
    if (selLight >= 0 && selLight < (int)m_scene.lights.size())
    {
        SceneLight& l = m_scene.lights[selLight];
        ImGui::Text("光源 #%d", selLight + 1);
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
    else if (selModel >= 0 && selModel < (int)m_scene.models.size())
    {
        SceneModel& m = m_scene.models[selModel];
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
            m_scene.DeleteSelected();
        }
    }
    else
    {
        ImGui::TextDisabled("未选中任何对象");
        ImGui::TextWrapped("在视口里点击物体，或在左侧“层级”列表中选择。");
    }
    ImGui::End();
}

// 视口面板：显示离屏场景纹理，同时记录面板矩形供拾取/拖拽使用
void EditorUI::ViewportUI()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 220.0f), ImVec2(1e9f, 1e9f));
    ImGui::Begin("视口", nullptr,
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 pos  = ImGui::GetWindowPos();
    ImVec2 min  = ImGui::GetWindowContentRegionMin();
    ImVec2 max  = ImGui::GetWindowContentRegionMax();

    m_viewport.MinX = pos.x + min.x;
    m_viewport.MinY = pos.y + min.y;
    m_viewport.W = (int)(max.x - min.x);
    m_viewport.H = (int)(max.y - min.y);
    if (m_viewport.W < 8) m_viewport.W = 8;
    if (m_viewport.H < 8) m_viewport.H = 8;

    GLuint displayTex = m_renderer.DisplayTexture();
    if (displayTex)
        ImGui::Image((ImTextureID)(intptr_t)displayTex,
                     ImVec2((float)m_viewport.W, (float)m_viewport.H),
                     ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));  // 翻转 Y
    else
        ImGui::TextDisabled("(无渲染目标)");
    ImGui::End();
    ImGui::PopStyleVar();
}
