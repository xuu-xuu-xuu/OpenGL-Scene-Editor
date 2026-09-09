#include "Texture.h"

#include <vector>
#include <utility>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// 用 stb_image 加载贴图（成功返回纹理 id）
GLuint LoadTextureFile(const std::string& path)
{
    stbi_set_flip_vertically_on_load(true);
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) return 0;
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return id;
}

// 简单纹理缓存：同一路径只加载一次
GLuint LoadTextureCached(const std::string& path)
{
    static std::vector<std::pair<std::string, GLuint>> cache;
    for (auto& kv : cache) if (kv.first == path) return kv.second;
    GLuint id = LoadTextureFile(path);
    if (id) cache.push_back(std::make_pair(path, id));
    return id;
}
