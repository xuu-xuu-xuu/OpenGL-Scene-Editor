#include "Picking.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "Platform.h"

namespace
{
constexpr float kAxisPickPixels = 14.0f;

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

// 模型世界空间 AABB（含平移 / 旋转 / 缩放）
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
} // namespace

// ---------- 相机矩阵与射线 ----------

void Picking::CurrentViewProj(GLFWwindow* window, glm::mat4& view, glm::mat4& proj) const
{
    view = m_camera->GetViewMatrix();
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    float aspect = (m_viewport->SceneFBH > 0)
        ? (float)m_viewport->SceneFBW / (float)m_viewport->SceneFBH
        : ((fbH > 0) ? (float)fbW / (float)fbH : 1.0f);
    proj = glm::perspective(glm::radians(m_settings->Fov), aspect, 0.05f, 500.0f);
}

void Picking::ScreenToRay(GLFWwindow* window, const glm::mat4& view, const glm::mat4& proj,
                          double mx, double my, glm::vec3& origin, glm::vec3& dir) const
{
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    origin = m_camera->Position;
    if (h <= 0) { dir = m_camera->Front; return; }
    glm::vec4 viewport(0.0f, 0.0f, (float)w, (float)h);
    glm::vec3 nearP = glm::unProject(glm::vec3((float)mx, (float)(h - my), 0.0f), view, proj, viewport);
    glm::vec3 farP  = glm::unProject(glm::vec3((float)mx, (float)(h - my), 1.0f), view, proj, viewport);
    dir = glm::normalize(farP - nearP);
}

// 拖动专用射线：允许鼠标越过黑边/场景矩形继续延伸，避免快速拖动“脱手”
void Picking::DragRay(GLFWwindow* window, const glm::mat4& view, const glm::mat4& proj,
                      double lx, double ly, glm::vec3& origin, glm::vec3& dir) const
{
    origin = m_camera->Position;
    if (m_viewport->SceneFBW <= 0 || m_viewport->SceneFBH <= 0) { dir = m_camera->Front; return; }
    float fx = (float)(lx * m_viewport->ScaleX);
    float fy = (float)(ly * m_viewport->ScaleY);
    float sx = fx - m_viewport->SceneFBX;
    float sy = fy - m_viewport->SceneFBY;
    glm::vec4 viewport(0.0f, 0.0f, (float)m_viewport->SceneFBW, (float)m_viewport->SceneFBH);
    glm::vec3 nearP = glm::unProject(glm::vec3(sx, (float)m_viewport->SceneFBH - sy, 0.0f), view, proj, viewport);
    glm::vec3 farP  = glm::unProject(glm::vec3(sx, (float)m_viewport->SceneFBH - sy, 1.0f), view, proj, viewport);
    dir = glm::normalize(farP - nearP);
}

int Picking::PickAxis(GLFWwindow* window, double mx, double my) const
{
    if (!m_scene->HasSelection()) return -1;
    if (!m_viewport->CursorInScene(mx, my)) return -1;

    glm::mat4 view, proj;
    CurrentViewProj(window, view, proj);
    glm::vec3 center = m_scene->SelPos();
    float len = m_scene->SelGizmoLen();

    // 场景内 framebuffer 像素坐标（相对场景矩形左上角）
    double cx = mx * m_viewport->ScaleX - m_viewport->SceneFBX;
    double cy = my * m_viewport->ScaleY - m_viewport->SceneFBY;

    auto projectScene = [&](const glm::vec3& w, double& sx, double& sy) -> bool
    {
        glm::vec4 clip = proj * view * glm::vec4(w, 1.0f);
        if (clip.w <= 0.0001f) return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < -1.0f || ndc.z > 1.0f) return false;
        sx = (ndc.x * 0.5f + 0.5f) * m_viewport->SceneFBW;
        sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * m_viewport->SceneFBH;
        return true;
    };

    float tol = kAxisPickPixels * (float)m_viewport->ScaleX;
    for (int axis = 0; axis < 3; ++axis)
    {
        glm::vec3 tip = center + m_scene->AxisDirWorld(axis) * len;
        double x0, y0, x1, y1;
        if (!projectScene(center, x0, y0)) continue;
        if (!projectScene(tip, x1, y1)) continue;
        if (PointSegmentDist2D(cx, cy, x0, y0, x1, y1) <= tol)
            return axis;
    }
    return -1;
}

// ---------- 鼠标交互 ----------

void Picking::OnMouseDown(GLFWwindow* window, double lx, double ly)
{
    m_leftDown = true;
    m_dragMoveEnabled = false;
    m_dragStarted = false;
    double mx = lx, my = ly;
    if (!m_viewport->CursorInScene(mx, my)) return;   // 黑边不拾取

    // 1) 已有选中物：先抓中心小球（自由移动），再抓坐标轴
    if (m_scene->HasSelection())
    {
        glm::mat4 vw, pj;
        CurrentViewProj(window, vw, pj);
        auto toPx = [&](const glm::vec3& w, float& sx, float& sy) -> bool
        {
            glm::vec4 clip = pj * vw * glm::vec4(w, 1.0f);
            if (clip.w <= 0.0001f) return false;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.z < -1.0f || ndc.z > 1.0f) return false;
            sx = (ndc.x * 0.5f + 0.5f) * m_viewport->SceneFBW;
            sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * m_viewport->SceneFBH;
            return true;
        };
        float bsx, bsy;
        if (toPx(m_scene->SelPos(), bsx, bsy))
        {
            float dx = (float)(mx * m_viewport->ScaleX - m_viewport->SceneFBX) - bsx;
            float dy = (float)(my * m_viewport->ScaleY - m_viewport->SceneFBY) - bsy;
            float tol = 20.0f * (float)m_viewport->ScaleX;
            if (dx * dx + dy * dy <= tol * tol)
            {
                m_grabAxis = 3;   // 3 = 中心球
                Platform::SetMouseCapture(window);
                m_dragMoveEnabled = true;
                m_dragStarted = true;
                m_grabStartPos = m_scene->SelPos();
                m_dragPlaneNormal = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
                    ? m_camera->Front : glm::vec3(0.0f, 1.0f, 0.0f);
                glm::vec3 o, d;
                ScreenToRay(window, vw, pj, mx, my, o, d);
                float tt = 0.0f;
                if (RayPlaneIntersect(o, d, m_scene->SelPos(), m_dragPlaneNormal, tt))
                    m_grabStartHit = o + d * tt;
                m_lastDragHit = m_grabStartHit;
                m_lastDragPos = m_grabStartPos;
                return;
            }
        }
    }
    // 1) 已有选中物：优先抓坐标轴（抓轴 = 立即允许拖动）
    int axis = PickAxis(window, mx, my);
    if (axis >= 0)
    {
        m_grabAxis = axis;
        Platform::SetMouseCapture(window);
        m_grabStartPos = m_scene->SelPos();
        m_dragMoveEnabled = true;
        m_dragStarted = true;
        m_dragPlaneNormal = m_camera->Front;
        glm::mat4 view, proj;
        CurrentViewProj(window, view, proj);
        glm::vec3 origin, dir;
        ScreenToRay(window, view, proj, mx, my, origin, dir);
        float t = 0.0f;
        if (RayPlaneIntersect(origin, dir, m_scene->SelPos(), m_dragPlaneNormal, t))
            m_grabStartHit = origin + dir * t;
        m_lastDragHit = m_grabStartHit;
        m_lastDragPos = m_grabStartPos;
        return;
    }

    // 2) 射线拾取：灯用球、模型用世界 AABB
    glm::mat4 view, proj;
    CurrentViewProj(window, view, proj);
    glm::vec3 origin, dir;
    ScreenToRay(window, view, proj, mx, my, origin, dir);

    float bestT = 1e30f;
    int bestLight = -1, bestModel = -1;
    for (int i = 0; i < (int)m_scene->lights.size(); ++i)
    {
        float t = 0.0f;
        if (RaySphereIntersect(origin, dir, m_scene->lights[i].Position, 0.55f, t) && t < bestT)
        { bestT = t; bestLight = i; bestModel = -1; }
    }
    for (int i = 0; i < (int)m_scene->models.size(); ++i)
    {
        glm::vec3 bmin, bmax;
        ModelWorldAABB(m_scene->models[i], bmin, bmax);
        float t = 0.0f;
        if (RayAABB(origin, dir, bmin, bmax, t) && t < bestT)
        { bestT = t; bestLight = -1; bestModel = i; }
    }

    // 远距离小目标：射线未命中时做屏幕空间就近兜底
    if (bestLight < 0 && bestModel < 0)
    {
        float cx = (float)(mx * m_viewport->ScaleX) - m_viewport->SceneFBX;
        float cy = (float)(my * m_viewport->ScaleY) - m_viewport->SceneFBY;
        float tol = 18.0f * (float)m_viewport->ScaleX;
        auto screenDist = [&](const glm::vec3& w) -> float
        {
            glm::vec4 clip = proj * view * glm::vec4(w, 1.0f);
            if (clip.w <= 0.0001f) return 1e9f;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.z < -1.0f || ndc.z > 1.0f) return 1e9f;
            float sx = (ndc.x * 0.5f + 0.5f) * m_viewport->SceneFBW;
            float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * m_viewport->SceneFBH;
            float dx = sx - cx;
            float dy = sy - cy;
            return dx * dx + dy * dy;
        };
        float bestD = 1e9f;
        for (int i = 0; i < (int)m_scene->lights.size(); ++i)
        {
            float d = screenDist(m_scene->lights[i].Position);
            if (d < bestD) { bestD = d; bestLight = i; bestModel = -1; }
        }
        for (int i = 0; i < (int)m_scene->models.size(); ++i)
        {
            float d = screenDist(m_scene->models[i].Pos);
            if (d < bestD) { bestD = d; bestLight = -1; bestModel = i; }
        }
        if (bestD > tol * tol) { bestLight = -1; bestModel = -1; }
    }
    if (bestLight >= 0) m_scene->SelectLight(bestLight);
    else if (bestModel >= 0) m_scene->SelectModel(bestModel);
    else { m_scene->ClearSelection(); return; }   // 点在所有选中框之外 => 取消选中
}

bool Picking::OnMouseMove(GLFWwindow* window, double lx, double ly)
{
    // 轴拖动：只沿该轴，增量跟随
    if (m_leftDown && m_grabAxis >= 0 && m_grabAxis <= 2 && m_scene->HasSelection())
    {
        glm::mat4 view, proj;
        CurrentViewProj(window, view, proj);
        glm::vec3 origin, dir;
        DragRay(window, view, proj, lx, ly, origin, dir);
        float t = 0.0f;
        if (RayPlaneIntersect(origin, dir, m_lastDragPos, m_camera->Front, t))
        {
            glm::vec3 hit = origin + dir * t;
            glm::vec3 axis = m_scene->AxisDirWorld(m_grabAxis);
            float delta = glm::dot(hit - m_lastDragHit, axis);
            glm::vec3 np = m_lastDragPos + axis * delta;
            m_lastDragHit = hit;
            m_lastDragPos = np;
            m_scene->SetSelPos(np);
        }
        return true;
    }

    // 中心球：自由移动，增量跟随（快速拖动也不会脱手）
    if (m_leftDown && m_grabAxis == 3 && m_scene->HasSelection() && m_dragMoveEnabled)
    {
        glm::mat4 view, proj;
        CurrentViewProj(window, view, proj);
        glm::vec3 origin, dir;
        DragRay(window, view, proj, lx, ly, origin, dir);
        float t = 0.0f;
        if (RayPlaneIntersect(origin, dir, m_lastDragPos, m_dragPlaneNormal, t))
        {
            glm::vec3 hit = origin + dir * t;
            glm::vec3 offset = hit - m_lastDragHit;
            offset -= m_dragPlaneNormal * glm::dot(offset, m_dragPlaneNormal);
            glm::vec3 np = m_lastDragPos + offset;
            if (m_scene->SelLight() >= 0 && np.y < 0.2f) np.y = 0.2f;
            m_lastDragHit = hit;
            m_lastDragPos = np;
            m_scene->SetSelPos(np);
        }
        return true;
    }
    return false;
}

void Picking::OnMouseUp(GLFWwindow* window)
{
    m_leftDown = false;
    Platform::ReleaseMouseCapture(window);
    m_grabAxis = -1;
    m_dragStarted = false;
    m_dragMoveEnabled = false;
}
