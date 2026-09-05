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