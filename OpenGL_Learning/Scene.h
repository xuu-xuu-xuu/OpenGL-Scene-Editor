#pragma once
// ============================================================
// Scene.h —— 场景数据 + 选择管理
// 职责：
//   1. 持有模型、光源列表及选择状态；
//   2. 提供增删模型/光源、选中/取消、选中位置读写等操作。
// 模型导入委托给 ModelImporter（见 ModelImporter.h）。
// ============================================================

#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "Mesh.h"

constexpr int kMaxLights = 8;

struct ModelSubMesh
{
    GLuint  TexId      = 0;
    bool    HasTexture = false;
    bool    SoftShade  = false;   // 脸/皮肤等材质用柔光
    unsigned int Start = 0;
    unsigned int Count = 0;
};

struct SceneModel
{
    bool    Valid = false;
    std::string Name;
    Mesh    mesh;                  // GPU 网格（VAO/VBO + 顶点数）
    glm::vec3 Color = glm::vec3(0.9f, 0.72f, 0.4f);
    glm::vec3 Pos   = glm::vec3(0.0f, 1.2f, 0.0f);
    float   Yaw    = 0.0f;
    float   Scale  = 1.0f;
    float   Radius = 1.3f;
    glm::vec3 BoundsMin = glm::vec3(-1.0f);
    glm::vec3 BoundsMax = glm::vec3(1.0f);
    bool    Selected = false;
    std::vector<ModelSubMesh> Subs;   // 按材质拆分的子网格
};

struct SceneLight
{
    glm::vec3 Position  = glm::vec3(-4.0f, 6.0f, 5.0f);
    glm::vec3 Color     = glm::vec3(1.0f, 0.95f, 0.85f);
    float   Intensity   = 1.6f;
    int     Type        = 0;   // 0=点光源 1=平行光 2=聚光灯
    glm::vec3 Direction = glm::vec3(0.0f, -1.0f, 0.0f);
    float   InnerDeg    = 20.0f;
    float   OuterDeg    = 35.0f;
    bool    Selected    = false;
};

class Scene
{
public:
    std::vector<SceneModel> models;
    std::vector<SceneLight> lights;

    // 坐标空间：世界 / 本地（用于 Gizmo）
    bool localSpace = false;

    // ---- 选择 ----
    int  SelModel() const { return m_selModel; }
    int  SelLight() const { return m_selLight; }
    bool HasSelection() const { return m_selModel >= 0 || m_selLight >= 0; }

    glm::vec3 SelPos() const;
    void SetSelPos(const glm::vec3& p);
    float SelGizmoLen() const { return (m_selModel >= 0) ? 2.2f : 1.3f; }
    glm::vec3 AxisDirWorld(int axis) const;

    void SelectModel(int i);
    void SelectLight(int i);
    void ClearSelection();

    // ---- 编辑 ----
    int  AddModelFile(const std::string& path);
    int  AddTorus();
    // 新增光源（放在相机前方 6 单位处）
    int  AddLight(int type, const glm::vec3& camPos, const glm::vec3& camFront);
    void DeleteSelected();
    // 清空场景（释放全部模型/灯光的 GPU 资源），供退出前清理
    void Clear();

private:
    int m_selModel = -1;
    int m_selLight = -1;
    int m_importCount = 0;   // 用于给新导入的模型循环分配调色板颜色
};
