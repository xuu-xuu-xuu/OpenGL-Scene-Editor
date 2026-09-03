#pragma once
// ============================================================
// ObjLoader.h —— OBJ 加载器（支持 v / vn / vt / f）
// 输出交错顶点：位置(3) + 法线(3) + UV(2)，每 8 个 float 一个顶点
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
    std::vector<float> Data;   // x,y,z, nx,ny,nz, u,v
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

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;
    struct Face { std::vector<int> p, n, t; };
    std::vector<Face> faces;

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
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
        else if (type == "vt")
        {
            float u = 0.0f, v = 0.0f;
            ss >> u >> v;
            texcoords.push_back(glm::vec2(u, v));
        }
        else if (type == "f")
        {
            Face f;
            std::string tok;
            while (ss >> tok)
            {
                int pi = -1, ni = -1, ti = -1;
                size_t s1 = tok.find('/');
                std::string a = (s1 == std::string::npos) ? tok : tok.substr(0, s1);
                if (!a.empty()) pi = std::atoi(a.c_str()) - 1;
                if (s1 != std::string::npos)
                {
                    size_t s2 = tok.find('/', s1 + 1);
                    std::string b = tok.substr(s1 + 1, s2 == std::string::npos ? std::string::npos : s2 - s1 - 1);
                    if (!b.empty()) ti = std::atoi(b.c_str()) - 1;
                    if (s2 != std::string::npos && s2 + 1 < tok.size())
                    {
                        std::string c = tok.substr(s2 + 1);
                        if (!c.empty()) ni = std::atoi(c.c_str()) - 1;
                    }
                }
                if (pi >= 0 && pi < (int)positions.size())
                {
                    f.p.push_back(pi);
                    f.n.push_back(ni);
                    f.t.push_back(ti);
                }
            }
            if (f.p.size() >= 3) faces.push_back(f);
        }
    }

    if (positions.empty() || faces.empty()) return false;

    out.Data.clear();
    out.BoundsMin = positions[0];
    out.BoundsMax = positions[0];

    auto pushVertex = [&](int pi, const glm::vec3& n, int ti)
    {
        const glm::vec3& p = positions[pi];
        glm::vec2 uv(0.0f);
        if (ti >= 0 && ti < (int)texcoords.size()) uv = texcoords[ti];
        out.Data.push_back(p.x); out.Data.push_back(p.y); out.Data.push_back(p.z);
        out.Data.push_back(n.x); out.Data.push_back(n.y); out.Data.push_back(n.z);
        out.Data.push_back(uv.x); out.Data.push_back(uv.y);
        out.BoundsMin = glm::min(out.BoundsMin, p);
        out.BoundsMax = glm::max(out.BoundsMax, p);
    };

    for (Face& f : faces)
    {
        int count = (int)f.p.size();
        bool hasNormals = true;
        for (int n : f.n)
            if (n < 0 || n >= (int)normals.size()) { hasNormals = false; break; }

        glm::vec3 flatN(0.0f);
        if (!hasNormals)
        {
            glm::vec3 a = positions[f.p[0]];
            glm::vec3 b = positions[f.p[1]];
            glm::vec3 c = positions[f.p[2]];
            flatN = glm::normalize(glm::cross(b - a, c - a));
        }

        for (int t = 1; t + 1 < count; ++t)
        {
            int idx[3]  = { f.p[0], f.p[t], f.p[t + 1] };
            int nidx[3] = { -1, -1, -1 };
            int tidx[3] = { f.t[0], f.t[t], f.t[t + 1] };
            if (hasNormals)
            {
                nidx[0] = f.n[0];
                nidx[1] = f.n[t];
                nidx[2] = f.n[t + 1];
            }
            for (int k = 0; k < 3; ++k)
            {
                glm::vec3 n = hasNormals ? normals[nidx[k]] : flatN;
                pushVertex(idx[k], n, tidx[k]);
            }
        }
    }

    out.VertexCount = (unsigned int)(out.Data.size() / 8);
    out.Loaded = true;
    return true;
}