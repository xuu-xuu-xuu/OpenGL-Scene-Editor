// ============================================================
// OpenGL 场景编辑器 —— 入口（薄壳）
// 所有初始化 / 主循环 / 回调 / 渲染 / 面板逻辑已按职责拆分到：
//   Application（编排） / Scene（数据） / Renderer（渲染）
//   EditorUI（界面） / Picking（拾取） / ModelImporter（导入）等。
// ============================================================

#include "Application.h"

int main()
{
    Application app;
    return app.Run();
}
