# OpenGL Scene Editor（三渲二风格实验）

用 C++ / OpenGL 从零实现的迷你场景编辑器，重点实验「三渲二（卡通渲染）」效果。
包含：多模型/多光源编辑、可停靠 ImGui 面板、屏幕空间描边、卡通色阶、后期调色。

## 技术栈

- C++17 / OpenGL 3.3 Core
- GLFW + GLEW
- Dear ImGui（docking 分支）
- GLM / stb_image
- 开发环境：Visual Studio 2022（Windows）

## 功能

- 视口面板：16:9 letterbox 离屏渲染 + 深度/法线缓冲
- 可停靠 GUI：工具栏 / 层级 / 属性 / 视口
- 模型导入：OBJ + mtl + map_Kd 贴图（按材质拆子网格）
- 光源：点光 / 平行光 / 聚光灯（箭头与锥体可视化）
- 风格化管线：
  - 三段式卡通色阶（可调断点、暗部色温、脸部柔光）
  - 屏幕空间黑色描边（深度 + 法线，带距离淡出）
  - 后期：饱和度 / 暖调 / 伪 AO / 泛光 / 颗粒 / 暗角


## 代码结构（模块化）

| 模块 | 文件 | 职责 |
|---|---|---|
| 入口/主循环 | `Application.cpp/h` | 窗口、输入、帧循环 |
| 平台层 | `Platform.cpp/h` | GLFW/系统交互、IME、鼠标捕获 |
| 渲染器 | `Renderer.cpp/h` | FBO、视口渲染、描边/后期管线 |
| 场景 | `Scene.cpp/h` | 模型实例、光源、选择状态 |
| 编辑器 UI | `EditorUI.cpp/h` | ImGui 工具栏/层级/属性面板 |
| 视口 | `Viewport.cpp/h` | 16:9 letterbox 与视口矩形 |
| 拾取/拖拽 | `Picking.cpp/h` | 射线拾取、Gizmo 手柄 |
| 模型导入 | `ModelImporter.cpp/h` | OBJ/MTL/贴图、子网格 |
| 网格 | `Mesh.cpp/h` | 几何生成与缓冲 |
| 纹理 | `Texture.cpp/h` | stb_image 加载 |
| 着色器 | `Shaders.h` | GLSL 源码集中定义 |
| 设置 | `Settings.h` | 全局参数/风格化参数 |
## 构建

所有第三方依赖（GLFW/GLEW/GLM/ImGui/stb_image 及静态库）都已内嵌在 `deps\` 目录，
工程内全部使用相对路径，下载后无需额外安装任何库。

1. 用 Visual Studio 2022 打开 `OpenGL_Learning.sln`
2. 直接选择 Debug/Release × x64/Win32 生成即可

## 操作

- WASD 移动相机，右键拖动转视角，滚轮缩放
- 左键点击物体选中；拖中心小球自由移动、拖红/绿/蓝轴沿轴移动
- O 导入模型 / T 圆环 / X 删除选中
- 全部编辑也可在左侧 ImGui 面板完成