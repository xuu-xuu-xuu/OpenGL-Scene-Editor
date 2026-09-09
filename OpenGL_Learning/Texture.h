#pragma once
// ============================================================
// Texture.h —— 贴图加载与缓存
// 负责：用 stb_image 读取图片文件 → 上传为 GL 纹理；
//      并提供“同路径只加载一次”的进程级缓存。
// ============================================================

#include <GL/glew.h>
#include <string>

// 加载一张贴图（成功返回纹理 id，失败返回 0）
GLuint LoadTextureFile(const std::string& path);

// 缓存版：同一路径只加载一次
GLuint LoadTextureCached(const std::string& path);
