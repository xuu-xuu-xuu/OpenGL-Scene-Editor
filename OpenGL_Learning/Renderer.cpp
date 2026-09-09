#include "Renderer.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Shaders.h"

namespace
{
// 把“局部指向 -Y”的物体对齐到目标方向 dir（用于灯头箭头 / 聚光锥）
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
} // namespace

Renderer::Renderer()
    : m_world(worldVertSrc, worldFragSrc),
      m_line(lineVertSrc, lineFragSrc),
      m_sky(skyVertSrc, skyFragSrc),
      m_flat(flatVertSrc, flatFragSrc),
      m_post(postVertSrc, postFragSrc),
      m_normal(normalVertSrc, normalFragSrc),
      m_cube(Mesh::Cube()),
      m_sphere(Mesh::Sphere(24, 12)),
      m_grid(Mesh::Grid()),
      m_axes(Mesh::Axes()),
      m_arrow(Mesh::Arrow()),
      m_cone(Mesh::SpotCone()),
      m_boxEdges(Mesh::BoxEdges())
{
    glGenVertexArrays(1, &m_postVao);

    m_world.Use();
    m_world.SetFloat("uShininess", 48.0f);

    // 演示场景里的装饰方块
    for (int i = 0; i < 6; ++i)
    {
        float angle = i / 6.0f * 2.0f * 3.14159265f;
        float scaleY = 0.8f + (i % 3) * 0.35f;
        Box b;
        b.Pos = glm::vec3(4.5f * cosf(angle), scaleY * 0.5f, 4.5f * sinf(angle));
        b.Scale = glm::vec3(1.0f, scaleY, 1.0f);
        b.Color = glm::vec3((i & 1) ? 0.95f : 0.25f, (i & 2) ? 0.45f : 0.85f, (i == 3) ? 0.95f : 0.35f);
        m_boxes.push_back(b);
    }
}

Renderer::~Renderer()
{
    if (m_postVao) glDeleteVertexArrays(1, &m_postVao);
    if (m_sceneColorTex) glDeleteTextures(1, &m_sceneColorTex);
    if (m_sceneDepthTex) glDeleteTextures(1, &m_sceneDepthTex);
    if (m_sceneNormalTex) glDeleteTextures(1, &m_sceneNormalTex);
    if (m_postTex) glDeleteTextures(1, &m_postTex);
    if (m_sceneFbo) glDeleteFramebuffers(1, &m_sceneFbo);
    if (m_postFbo) glDeleteFramebuffers(1, &m_postFbo);
}

void Renderer::RecreateTarget(int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if (m_sceneColorTex) glDeleteTextures(1, &m_sceneColorTex);
    if (m_sceneDepthTex) glDeleteTextures(1, &m_sceneDepthTex);
    if (m_sceneNormalTex) glDeleteTextures(1, &m_sceneNormalTex);
    if (m_postTex) glDeleteTextures(1, &m_postTex);
    if (m_sceneFbo) glDeleteFramebuffers(1, &m_sceneFbo);
    if (m_postFbo) glDeleteFramebuffers(1, &m_postFbo);
    m_fboW = w; m_fboH = h;

    // 场景颜色
    glGenTextures(1, &m_sceneColorTex);
    glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 场景深度（纹理，供描边采样）
    glGenTextures(1, &m_sceneDepthTex);
    glBindTexture(GL_TEXTURE_2D, m_sceneDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 法线纹理（视空间法线打包到 0..1）
    glGenTextures(1, &m_sceneNormalTex);
    glBindTexture(GL_TEXTURE_2D, m_sceneNormalTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 后处理颜色
    glGenTextures(1, &m_postTex);
    glBindTexture(GL_TEXTURE_2D, m_postTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 场景 FBO：颜色 + 深度纹理
    glGenFramebuffers(1, &m_sceneFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneColorTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_sceneDepthTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_sceneNormalTex, 0);

    // 后处理 FBO
    glGenFramebuffers(1, &m_postFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_postFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_postTex, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::ApplyLightUniforms(const Scene& scene)
{
    int lightCount = (int)scene.lights.size();
    if (lightCount > kMaxLights) lightCount = kMaxLights;
    m_world.SetInt("uLightCount", lightCount);
    if (lightCount > 0)
    {
        std::vector<glm::vec3> posArr, colArr, dirArr;
        std::vector<float> typeArr, coneInArr, coneOutArr, intArr;
        for (int i = 0; i < lightCount; ++i)
        {
            posArr.push_back(scene.lights[i].Position);
            colArr.push_back(scene.lights[i].Color);
            intArr.push_back(scene.lights[i].Intensity);
            typeArr.push_back((float)scene.lights[i].Type);
            dirArr.push_back(scene.lights[i].Direction);
            float cin = cos(glm::radians(std::min(scene.lights[i].InnerDeg, scene.lights[i].OuterDeg)));
            float cout = cos(glm::radians(std::max(scene.lights[i].InnerDeg, scene.lights[i].OuterDeg)));
            coneInArr.push_back(cin);
            coneOutArr.push_back(cout);
        }
        m_world.SetVec3Array("uLightPos", posArr.data(), lightCount);
        m_world.SetVec3Array("uLightColor", colArr.data(), lightCount);
        m_world.SetFloatArray("uLightIntensity", intArr.data(), lightCount);
        m_world.SetFloatArray("uLightTypeF", typeArr.data(), lightCount);
        m_world.SetVec3Array("uLightDir", dirArr.data(), lightCount);
        m_world.SetFloatArray("uLightConeIn", coneInArr.data(), lightCount);
        m_world.SetFloatArray("uLightConeOut", coneOutArr.data(), lightCount);
    }
}

// 按材质子网格绘制
void Renderer::DrawModelRanges(const Shader& sh, const SceneModel& m,
                               const glm::mat4& model, const glm::vec3& color)
{
    sh.SetMat4("uModel", model);
    glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(model)));
    sh.SetMat3("uNormalMat", normalMat);
    sh.SetVec3("uColor", color);
    glBindVertexArray(m.mesh.Vao());
    if (m.Subs.empty())
    {
        sh.SetInt("uUseTex", 0);
        sh.SetInt("uSoftShade", 0);
        glDrawArrays(GL_TRIANGLES, 0, m.mesh.Count());
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

// 单段网格绘制（无贴图）
void Renderer::DrawMesh(const Shader& shader, GLuint vao, GLsizei count,
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

void Renderer::Render(const Scene& scene, const Camera& camera,
                      const RenderSettings& settings, const Viewport& viewport,
                      float time, int fbW, int fbH)
{
    glm::mat4 view = camera.GetViewMatrix();
    float aspect = (viewport.SceneFBH > 0)
        ? (float)viewport.SceneFBW / (float)viewport.SceneFBH
        : ((fbH > 0) ? (float)fbW / (float)fbH : 1.0f);
    glm::mat4 proj = glm::perspective(glm::radians(settings.Fov), aspect, 0.05f, 500.0f);

    int targetW = (viewport.FBW > 0) ? viewport.FBW : fbW;
    int targetH = (viewport.FBH > 0) ? viewport.FBH : fbH;
    if (targetW != m_fboW || targetH != m_fboH) RecreateTarget(targetW, targetH);

    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
    glViewport(0, 0, m_fboW, m_fboH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);          // 黑边
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 只把 16:9 场景画在中间区域（未就绪时退化为全幅）
    int sx = (viewport.SceneFBW > 0) ? viewport.SceneFBX : 0;
    int syTop = (viewport.SceneFBW > 0) ? viewport.SceneFBY : 0;
    int sw = (viewport.SceneFBW > 0) ? viewport.SceneFBW : m_fboW;
    int sh = (viewport.SceneFBH > 0) ? viewport.SceneFBH : m_fboH;
    int sy = m_fboH - syTop - sh;
    glEnable(GL_SCISSOR_TEST);
    glViewport(sx, sy, sw, sh);
    glScissor(sx, sy, sw, sh);
    glClearColor(0.06f, 0.07f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, settings.Wireframe ? GL_LINE : GL_FILL);

    // ---- 世界着色器 ----
    m_world.Use();
    m_world.SetMat4("uView", view);
    m_world.SetMat4("uProj", proj);
    m_world.SetVec3("uViewPos", camera.Position);
    m_world.SetFloat("uToon", settings.Toon ? 1.0f : 0.0f);
    m_world.SetFloat("uBandHi", settings.BandHi);
    m_world.SetFloat("uBandMid", settings.BandMid);
    m_world.SetFloat("uBandLo", settings.BandLo);
    m_world.SetVec3("uShadowTint", settings.ShadowTint);
    m_world.SetFloat("uShadowAmt", settings.ShadowAmt);
    m_world.SetVec3("uSpecColor", settings.SpecColor);
    m_world.SetFloat("uRimAmt", settings.RimAmt);
    m_world.SetFloat("uFaceFill", settings.FaceFill);

    ApplyLightUniforms(scene);

    // 装饰方块
    for (const Box& box : m_boxes)
    {
        glm::mat4 model(1.0f);
        model = glm::translate(model, box.Pos);
        model = glm::scale(model, box.Scale);
        DrawMesh(m_world, m_cube.Vao(), m_cube.Count(), model, box.Color);
    }

    // 模型
    for (const SceneModel& m : scene.models)
    {
        if (!m.Valid) continue;
        glm::mat4 model(1.0f);
        model = glm::translate(model, m.Pos);
        model = glm::rotate(model, glm::radians(m.Yaw), glm::vec3(0, 1, 0));
        model = glm::scale(model, glm::vec3(m.Scale));
        DrawModelRanges(m_world, m, model, m.Color);
    }

    // 光源 gizmo 球
    m_flat.Use();
    for (const SceneLight& l : scene.lights)
    {
        glm::mat4 gizmo(1.0f);
        gizmo = glm::translate(gizmo, l.Position);
        float s = l.Selected ? 0.55f : 0.42f;
        gizmo = glm::scale(gizmo, glm::vec3(s));
        m_flat.SetMat4("uMvp", proj * view * gizmo);
        m_flat.SetVec3("uColor", l.Selected ? glm::vec3(1.0f, 0.9f, 0.35f) : l.Color);
        m_sphere.Draw(GL_TRIANGLES);
    }

    // Gizmo 坐标轴
    if (scene.HasSelection())
    {
        m_line.Use();
        m_line.SetMat4("uView", view);
        m_line.SetMat4("uProj", proj);
        glm::mat4 g(1.0f);
        g = glm::translate(g, scene.SelPos());
        if (scene.localSpace && scene.SelModel() >= 0)
            g = glm::rotate(g, glm::radians(scene.models[scene.SelModel()].Yaw), glm::vec3(0, 1, 0));
        g = glm::scale(g, glm::vec3(scene.SelGizmoLen()));
        m_line.SetMat4("uModel", g);
        m_axes.Draw(GL_LINES);
    }

    // 平行光/聚光灯：方向箭头
    m_line.Use();
    m_line.SetMat4("uView", view);
    m_line.SetMat4("uProj", proj);
    for (const SceneLight& l : scene.lights)
    {
        if (l.Type < 1) continue;
        glm::mat4 am(1.0f);
        am = glm::translate(am, l.Position);
        am = am * glm::mat4(AlignToDirection(l.Direction));
        am = glm::scale(am, glm::vec3(1.8f));
        m_line.SetMat4("uModel", am);
        m_arrow.Draw(GL_LINES);
        // 聚光灯：外锥可视化
        if (l.Type == 2)
        {
            float len = 1.8f;
            float tr = tanf(glm::radians(std::max(l.OuterDeg, 0.5f))) * len;
            glm::mat4 cm(1.0f);
            cm = glm::translate(cm, l.Position);
            cm = cm * glm::mat4(AlignToDirection(l.Direction));
            cm = glm::scale(cm, glm::vec3(tr, len, tr));
            m_line.SetMat4("uModel", cm);
            m_cone.Draw(GL_LINES);
        }
    }

    // 选中物体：中心小球 = 自由移动手柄
    if (scene.HasSelection())
    {
        m_flat.Use();
        glm::mat4 ballM(1.0f);
        ballM = glm::translate(ballM, scene.SelPos());
        ballM = glm::scale(ballM, glm::vec3(0.18f));
        m_flat.SetMat4("uMvp", proj * view * ballM);
        m_flat.SetVec3("uColor", glm::vec3(1.0f, 0.96f, 0.88f));
        m_sphere.Draw(GL_TRIANGLES);
    }

    // 选中模型的橙色包围盒线框
    if (scene.SelModel() >= 0 && scene.SelModel() < (int)scene.models.size())
    {
        const SceneModel& sm = scene.models[scene.SelModel()];
        glm::vec3 size = sm.BoundsMax - sm.BoundsMin;
        glm::mat4 bm(1.0f);
        bm = glm::translate(bm, sm.Pos);
        bm = glm::rotate(bm, glm::radians(sm.Yaw), glm::vec3(0, 1, 0));
        bm = glm::scale(bm, size);
        m_line.Use();
        m_line.SetMat4("uView", view);
        m_line.SetMat4("uProj", proj);
        m_line.SetMat4("uModel", bm);
        m_boxEdges.Draw(GL_LINES);
    }

    // 网格
    if (settings.ShowGrid)
    {
        m_line.Use();
        m_line.SetMat4("uView", view);
        m_line.SetMat4("uProj", proj);
        m_line.SetMat4("uModel", glm::mat4(1.0f));
        m_grid.Draw(GL_LINES);
    }

    // 天空盒
    if (settings.ShowSky)
    {
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        m_sky.Use();
        m_sky.SetMat4("uView", view);
        m_sky.SetMat4("uProj", proj);
        glm::vec3 sunDir(0.3f, 0.8f, 0.4f);   // 默认：太阳固定不动
        if (settings.SunFollowLight && !scene.lights.empty())
            sunDir = glm::length(scene.lights[0].Position) > 0.01f ? glm::normalize(scene.lights[0].Position) : sunDir;
        m_sky.SetVec3("uSunDir", sunDir);
        m_cube.Draw(GL_TRIANGLES);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    }

    // ---- 法线通道：内部折痕描边用 ----
    if (m_sceneNormalTex)
    {
        glDrawBuffer(GL_COLOR_ATTACHMENT1);
        glDepthFunc(GL_LEQUAL);
        glClearColor(0.5f, 0.5f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        m_normal.Use();
        m_normal.SetMat4("uView", view);
        m_normal.SetMat4("uProj", proj);
        for (const Box& box : m_boxes)
        {
            glm::mat4 model(1.0f);
            model = glm::translate(model, box.Pos);
            model = glm::scale(model, box.Scale);
            DrawMesh(m_normal, m_cube.Vao(), m_cube.Count(), model, box.Color);
        }
        for (const SceneModel& m : scene.models)
        {
            if (!m.Valid) continue;
            glm::mat4 model(1.0f);
            model = glm::translate(model, m.Pos);
            model = glm::rotate(model, glm::radians(m.Yaw), glm::vec3(0, 1, 0));
            model = glm::scale(model, glm::vec3(m.Scale));
            DrawMesh(m_normal, m.mesh.Vao(), m.mesh.Count(), model, m.Color);
        }
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glDepthFunc(GL_LESS);
    }

    // ---- 屏幕空间描边后处理 ----
    if (m_postFbo && m_sceneColorTex && m_sceneDepthTex)
    {
        glDisable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, m_postFbo);
        glViewport(0, 0, m_fboW, m_fboH);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        m_post.Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_sceneColorTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_sceneDepthTex);
        m_post.SetInt("uScene", 0);
        m_post.SetInt("uDepth", 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_sceneNormalTex);
        m_post.SetInt("uNormal", 2);
        m_post.SetFloat("uOutlineOn", settings.Outline ? 1.0f : 0.0f);
        m_post.SetFloat("uGradeOn", settings.Grade ? 1.0f : 0.0f);
        m_post.SetFloat("uSatAmt", settings.SatAmt);
        m_post.SetFloat("uTime", time);
        m_post.SetFloat("uBloomAmt", settings.BloomAmt);
        m_post.SetFloat("uGrainAmt", settings.GrainAmt);
        glBindVertexArray(m_postVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glEnable(GL_DEPTH_TEST);
    }

    // 切回默认缓冲，清屏准备画 ImGui
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbW, fbH);
    glClearColor(0.10f, 0.11f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}
