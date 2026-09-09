#pragma once
// ============================================================
// ModelImporter.h —— OBJ 模型导入器
// 职责：把一个 .obj 文件（含 .mtl 材质与贴图）解析成可渲染的
//       SceneModel（网格 + 材质子网格 + 包围盒）。
// ============================================================

#include <string>

#include "Scene.h"

class ModelImporter
{
public:
    // 完整导入：加载 OBJ、解析 MTL、加载贴图、按材质拆子网格。
    // 成功填充 out 并返回 true；失败返回 false 且不改动场景。
    bool Load(const std::string& path, SceneModel& out);

    // 从完整路径提取文件名
    static std::string FileNameOf(const std::string& path);
};
