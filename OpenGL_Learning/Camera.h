#pragma once
// ============================================================
// Camera.h —— 第一人称自由相机（FPS 风格）
// 用法：
//   1. 每帧根据按键调用 Move(forward, right, up, dt, boost)
//   2. 鼠标移动时调用 Rotate(dx, dy)
//   3. 绘制时调用 GetViewMatrix() 得到视图矩阵
// ============================================================

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera
{
public:
    // 相机在世界空间中的位置
    glm::vec3 Position = glm::vec3(8.0f, 6.0f, 12.0f);

    // 三个互相垂直的方向向量（由 Yaw/Pitch 每帧重新计算）
    glm::vec3 Front = glm::vec3(0.0f, 0.0f, -1.0f);   // 相机看向哪
    glm::vec3 Right = glm::vec3(1.0f, 0.0f, 0.0f);    // 相机右侧
    glm::vec3 Up    = glm::vec3(0.0f, 1.0f, 0.0f);    // 相机正上方

    // 欧拉角：Yaw 左右转头，Pitch 上下点头（单位：度）
    float Yaw   = -90.0f;   // -90 表示初始正对 -Z 方向
    float Pitch = -12.0f;   // 稍微往下看，方便观察地面

    // 手感参数
    float MoveSpeed       = 7.0f;    // 基础移动速度（单位/秒）
    float BoostMultiplier = 2.5f;    // 按住 Shift 的加速倍率
    float MouseSensitivity = 0.12f;  // 鼠标灵敏度

    // 根据 Yaw/Pitch 重新计算 Front / Right / Up
    // 原理：球坐标。先算朝向向量，再叉积推出右侧和上方
    void UpdateVectors()
    {
        glm::vec3 front;
        front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        front.y = sin(glm::radians(Pitch));
        front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        Front = glm::normalize(front);
        Right = glm::normalize(glm::cross(Front, glm::vec3(0.0f, 1.0f, 0.0f)));
        Up    = glm::normalize(glm::cross(Right, Front));
    }

    // 平移相机。
    // forward / right / up 传入 -1、0 或 1（例如按 W 传 forward=1）
    // dt 是上一帧耗时（秒），乘以 dt 后移动速度就与帧率无关了
    void Move(int forward, int right, int up, float dt, bool boost)
    {
        float speed = MoveSpeed * (boost ? BoostMultiplier : 1.0f) * dt;
        Position += Front                     * (float)forward * speed;
        Position += Right                     * (float)right   * speed;
        Position += glm::vec3(0.0f, 1.0f, 0.0f) * (float)up      * speed;
    }

    // 转动视角。dx/dy 是鼠标相对上次位置的像素差
    void Rotate(float dx, float dy)
    {
        Yaw   += dx * MouseSensitivity;
        Pitch -= dy * MouseSensitivity;   // 鼠标向下拖 => 视角向下 => Pitch 减小
        // 限制上下视角范围，避免翻转
        if (Pitch >  89.0f) Pitch =  89.0f;
        if (Pitch < -89.0f) Pitch = -89.0f;
        UpdateVectors();
    }

    // 视图矩阵：把世界坐标变换到“以相机为原点”的坐标系
    // lookAt(相机位置, 相机看向的目标点, 上方向)
    glm::mat4 GetViewMatrix() const
    {
        return glm::lookAt(Position, Position + Front, Up);
    }
};