#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "ModelImporter.h"

// ---------- 选择管理 ----------

glm::vec3 Scene::SelPos() const
{
    if (m_selModel >= 0) return models[m_selModel].Pos;
    if (m_selLight >= 0) return lights[m_selLight].Position;
    return glm::vec3(0.0f);
}

void Scene::SetSelPos(const glm::vec3& p)
{
    if (m_selModel >= 0) models[m_selModel].Pos = p;
    if (m_selLight >= 0) lights[m_selLight].Position = p;
}

glm::vec3 Scene::AxisDirWorld(int axis) const
{
    glm::vec3 e = (axis == 0) ? glm::vec3(1, 0, 0)
                : (axis == 1) ? glm::vec3(0, 1, 0)
                              : glm::vec3(0, 0, 1);
    if (localSpace && m_selModel >= 0)
    {
        float yaw = glm::radians(models[m_selModel].Yaw);
        glm::mat4 r = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0));
        e = glm::vec3(r * glm::vec4(e, 0.0f));
    }
    return e;
}

void Scene::ClearSelection()
{
    if (m_selModel >= 0 && m_selModel < (int)models.size()) models[m_selModel].Selected = false;
    if (m_selLight >= 0 && m_selLight < (int)lights.size()) lights[m_selLight].Selected = false;
    m_selModel = -1;
    m_selLight = -1;
}

void Scene::SelectModel(int i)
{
    ClearSelection();
    if (i >= 0 && i < (int)models.size())
    {
        m_selModel = i;
        models[i].Selected = true;
    }
}

void Scene::SelectLight(int i)
{
    ClearSelection();
    if (i >= 0 && i < (int)lights.size())
    {
        m_selLight = i;
        lights[i].Selected = true;
    }
}

// ---------- 编辑操作 ----------

int Scene::AddModelFile(const std::string& path)
{
    ModelImporter importer;
    SceneModel m;
    if (!importer.Load(path, m)) return -1;

    const glm::vec3 palette[] = {
        glm::vec3(0.90f, 0.72f, 0.40f), glm::vec3(0.55f, 0.75f, 0.95f),
        glm::vec3(0.85f, 0.45f, 0.45f), glm::vec3(0.55f, 0.85f, 0.60f),
        glm::vec3(0.75f, 0.60f, 0.90f)
    };
    m.Color = palette[m_importCount % 5];
    m_importCount++;
    float a = (float)models.size() * 2.39996f;
    m.Pos = glm::vec3(cosf(a) * 2.5f, 1.2f, sinf(a) * 2.5f);
    models.push_back(std::move(m));
    std::cout << "[添加模型] " << models.back().Name << "（"
              << models.back().mesh.Count() << " 顶点）" << std::endl;
    return (int)models.size() - 1;
}

int Scene::AddTorus()
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

int Scene::AddLight(int type, const glm::vec3& camPos, const glm::vec3& camFront)
{
    if ((int)lights.size() >= kMaxLights)
    {
        std::cout << "[光源] 已达上限 " << kMaxLights << "，请先删除" << std::endl;
        return -1;
    }
    SceneLight l;
    l.Type = type;
    if (type == 1) l.Direction = glm::normalize(glm::vec3(0.35f, 0.8f, 0.45f));
    if (type == 2) l.Direction = glm::vec3(0.0f, -1.0f, 0.0f);
    l.Position = camPos + camFront * 6.0f;
    l.Position.y = std::max(l.Position.y, 1.0f);
    lights.push_back(l);
    int idx = (int)lights.size() - 1;
    std::cout << "[添加光源] 第 " << idx + 1 << " 盏" << std::endl;
    return idx;
}

void Scene::DeleteSelected()
{
    if (m_selModel >= 0 && m_selModel < (int)models.size())
    {
        std::cout << "[删除模型] " << models[m_selModel].Name << std::endl;
        models.erase(models.begin() + m_selModel);
        ClearSelection();
    }
    else if (m_selLight >= 0 && m_selLight < (int)lights.size())
    {
        std::cout << "[删除光源] 第 " << m_selLight + 1 << " 盏" << std::endl;
        lights.erase(lights.begin() + m_selLight);
        ClearSelection();
    }
    else std::cout << "[删除] 当前没有选中任何物体" << std::endl;
}

void Scene::Clear()
{
    models.clear();
    lights.clear();
    m_selModel = -1;
    m_selLight = -1;
}
