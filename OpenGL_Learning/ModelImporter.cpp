#include "ModelImporter.h"

#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include <glm/glm.hpp>

#include "ObjLoader.h"
#include "Texture.h"

namespace
{
// 加载 OBJ 并按“包围盒中心 + 缩放适配”居中归一化
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
} // namespace

std::string ModelImporter::FileNameOf(const std::string& path)
{
    size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

bool ModelImporter::Load(const std::string& path, SceneModel& out)
{
    ObjModel obj;
    if (!LoadAndCenterObj(path, obj)) return false;

    out.Valid = true;
    out.Name = FileNameOf(path);
    out.mesh = Mesh(obj.Data, Mesh::Layout::PositionNormalUV);
    out.Radius = obj.Radius();
    out.BoundsMin = obj.BoundsMin;
    out.BoundsMax = obj.BoundsMax;
    out.Subs.clear();

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
        out.Subs.push_back(sm);
    }
    if (out.Subs.empty())
    {
        ModelSubMesh sm;
        sm.Start = 0;
        sm.Count = obj.VertexCount;
        out.Subs.push_back(sm);
    }

    return true;
}
