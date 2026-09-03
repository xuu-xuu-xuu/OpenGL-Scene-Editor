#pragma once
// ============================================================
// ObjLoader.h —— 极简 OBJ 模型加载器
//
// 支持的 OBJ 语法：
//   v  x y z        （顶点位置）
//   vn x y z        （顶点法线）
//   f 1//1 2//2 3//3  或  f 1/1/1 2/2/1 3/3/1  或  f 1 2 3
//
// 特性：
//   - 没有法线的模型会自动生成“平面法线”（每个面一个方向）
//   - 多边形面会自动三角化
//   - 输出交错的 position+normal 顶点数组，直接 glDrawArrays
// ============================================================

#include <glm/glm.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct ObjModel
{
    bool Loaded = false;
    std::vector<float> Data;   // 交错数据：x,y,z,nx,ny,nz（每 6 个 float = 1 个顶点）
    unsigned int VertexCount = 0;
    glm::vec3 BoundsMin = glm::vec3(0.0f);
    glm::vec3 BoundsMax = glm::vec3(0.0f);

    glm::vec3 Center() const { return (BoundsMin + BoundsMax) * 0.5f; }
    float Radius() const     { return glm::length(BoundsMax - BoundsMin) * 0.5f; }
};

inline bool LoadObjFile(const std::string& path, ObjModel& out)
{
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::vector<glm::vec3> positions;                    // OBJ 里所有 v
    std::vector<glm::vec3> normals;                      // OBJ 里所有 vn
    std::vector<std::pair<std::vector<int>, std::vector<int>>> faces;  // (顶点下标表, 法线下标表)

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // 兼容 CRLF 换行
        std::istringstream ss(line);
        std::string type;
        ss >> type;

        if (type == "v")
        {
            float x, y, z;
            if (ss >> x >> y >> z) positions.push_back(glm::vec3(x, y, z));
        }
        else if (type == "vn")
        {
            float x, y, z;
            if (ss >> x >> y >> z) normals.push_back(glm::vec3(x, y, z));
        }
        else if (type == "f")
        {
            std::vector<int> pi;   // 本面的顶点下标（0-based）
            std::vector<int> ni;   // 本面的法线下标（没有则为 -1）
            std::string token;
            while (ss >> token)
            {
                // token 可能是: "7"  "7//7"  "7/1/7"
                size_t first = token.find('/');
                int pidx = -1, nidx = -1;

                std::string posStr = (first == std::string::npos) ? token : token.substr(0, first);
                if (!posStr.empty()) pidx = std::atoi(posStr.c_str()) - 1;   // OBJ 从 1 开始数

                if (first != std::string::npos)
                {
                    size_t second = token.find('/', first + 1);
                    if (second != std::string::npos && second + 1 < token.size())
                    {
                        std::string nStr = token.substr(second + 1);
                        if (!nStr.empty()) nidx = std::atoi(nStr.c_str()) - 1;
                    }
                }

                if (pidx >= 0 && pidx < (int)positions.size())
                {
                    pi.push_back(pidx);
                    ni.push_back(nidx);
                }
            }
            if (pi.size() >= 3)
            {
                faces.push_back(std::make_pair(pi, ni));
            }
        }
    }

    if (positions.empty() || faces.empty()) return false;

    out.Data.clear();
    out.BoundsMin = positions[0];
    out.BoundsMax = positions[0];

    auto pushVertex = [&](const glm::vec3& pos, const glm::vec3& nrm)
    {
        out.Data.push_back(pos.x); out.Data.push_back(pos.y); out.Data.push_back(pos.z);
        out.Data.push_back(nrm.x); out.Data.push_back(nrm.y); out.Data.push_back(nrm.z);
        out.BoundsMin = glm::min(out.BoundsMin, pos);
        out.BoundsMax = glm::max(out.BoundsMax, pos);
    };

    for (auto& face : faces)
    {
        const std::vector<int>& pi = face.first;
        const std::vector<int>& ni = face.second;
        int count = (int)pi.size();

        // 是否所有顶点都带法线；有缺失就用“平面法线”代替
        bool hasNormals = true;
        for (int n : ni)
        {
            if (n < 0 || n >= (int)normals.size()) { hasNormals = false; break; }
        }

        // 平面法线 = 面的前三个顶点叉积得到的方向（没有法线信息时使用）
        glm::vec3 flatN(0.0f);
        if (!hasNormals)
        {
            glm::vec3 a = positions[pi[0]];
            glm::vec3 b = positions[pi[1]];
            glm::vec3 c = positions[pi[2]];
            flatN = glm::normalize(glm::cross(b - a, c - a));
        }

        // 扇形三角化：把多边形拆成 (0,1,2) (0,2,3) (0,3,4) ... 这样的三角形
        for (int t = 1; t + 1 < count; ++t)
        {
            int idx[3]  = { pi[0], pi[t], pi[t + 1] };   // 三个角对应的顶点下标
            int nidx[3] = { -1, -1, -1 };                // 三个角对应的法线下标
            if (hasNormals)
            {
                nidx[0] = ni[0];
                nidx[1] = ni[t];
                nidx[2] = ni[t + 1];
            }

            for (int k = 0; k < 3; ++k)
            {
                glm::vec3 normal = hasNormals ? normals[nidx[k]] : flatN;
                pushVertex(positions[idx[k]], normal);
            }
        }
    }

    out.VertexCount = (unsigned int)(out.Data.size() / 6);
    out.Loaded = true;
    return true;
}