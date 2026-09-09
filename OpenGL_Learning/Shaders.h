#pragma once
// ============================================================
// Shaders.h —— 全部着色器源码字符串
// 集中管理，避免堆在渲染代码里。均为编译期常量。
// ============================================================

static const char*worldVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
out vec2 vUV;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMat;
out vec3 vWorldPos;
out vec3 vNormal;
void main()
{
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = vec3(world);
    vNormal   = uNormalMat * aNormal;
    vUV = aUV;
    gl_Position = uProj * uView * world;
}
)";

static const char*worldFragSrc = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
out vec4 FragColor;

uniform vec3 uColor;
uniform sampler2D uTex;
uniform int uUseTex;
uniform int uSoftShade;
uniform vec3 uViewPos;
uniform float uShininess;
uniform float uToon;
uniform float uBandHi;
uniform float uBandMid;
uniform float uBandLo;
uniform vec3 uShadowTint;
uniform float uShadowAmt;
uniform vec3 uSpecColor;
uniform float uRimAmt;
uniform float uFaceFill;
const int kMaxLights = 8;
uniform int uLightCount;
uniform vec3 uLightPos[kMaxLights];
uniform vec3 uLightColor[kMaxLights];
uniform float uLightIntensity[kMaxLights];
uniform float uLightTypeF[kMaxLights];
uniform vec3 uLightDir[kMaxLights];
uniform float uLightConeIn[kMaxLights];
uniform float uLightConeOut[kMaxLights];

void main()
{
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 baseColor = (uUseTex > 0) ? texture(uTex, vUV).rgb : uColor;
    vec3 result = (uToon > 0.5f)
        ? vec3(0.20f, 0.19f, 0.21f) * baseColor
        : (0.16f + 0.08f * max(N.y, 0.0f)) * baseColor;
    for (int i = 0; i < kMaxLights; ++i)
    {
        if (i >= uLightCount) break;

        vec3 L;
        float atten = 1.0f;
        if (uLightTypeF[i] < 0.5f)
        {
            // 点光源：距离衰减
            vec3 toL = uLightPos[i] - vWorldPos;
            float dist = length(toL);
            atten = 1.0f / (1.0f + 0.09f * dist + 0.05f * dist * dist);
            if (atten < 0.18f) atten = 0.18f;
            L = normalize(toL);
        }
        else if (uLightTypeF[i] < 1.5f)
        {
            // 平行光：只取方向，无衰减
            L = normalize(uLightDir[i]);
        }
        else
        {
            // 聚光灯：距离衰减 + 锥角衰减
            vec3 toL = uLightPos[i] - vWorldPos;
            float dist = length(toL);
            atten = 1.0f / (1.0f + 0.09f * dist + 0.05f * dist * dist);
            if (atten < 0.18f) atten = 0.18f;
            L = normalize(toL);
            float cosA = max(dot(-L, normalize(uLightDir[i])), 0.0f);
            atten *= smoothstep(uLightConeOut[i], uLightConeIn[i], cosA);
        }

        vec3 H = normalize(L + V);
        float ndl = max(dot(N, L), 0.0);
        float diff = ndl;
        if (uToon > 0.5f)
        {
            if (uSoftShade > 0)
            {
                // 脸/皮肤：柔和受光，暗部不会死黑
                diff = 0.48f + 0.40f * smoothstep(0.08f, 0.55f, ndl);   // 脸：与身体亮度协调
            }
            else
            {
                float bLo = mix(0.14f, 0.32f, smoothstep(uBandLo - 0.015f, uBandLo + 0.015f, ndl));
                float bMid = mix(bLo, 0.64f, smoothstep(uBandMid - 0.025f, uBandMid + 0.025f, ndl));
                diff = mix(bMid, 1.0f, smoothstep(uBandHi - 0.03f, uBandHi + 0.03f, ndl));
            }
        }
        float ndh = max(dot(N, H), 0.0);
        float spec = pow(ndh, uShininess);
        spec = smoothstep(0.46f, 0.56f, ndh) * pow(ndh, 340.0f) * 0.55f;   // 高光更小更硬
        if (uSoftShade > 0) spec *= 0.25f;   // 脸/皮肤哑光，去塑料感

        vec3 radiance = uLightColor[i] * uLightIntensity[i] * atten;
        result += radiance * (diff * baseColor + spec * uSpecColor);
        float darkAmt = (uToon > 0.5f) ? clamp(1.0f - diff, 0.0f, 1.0f) : 0.0f;
        result += radiance * uShadowTint * darkAmt * uShadowAmt * 0.35f;
    }
    if (uToon > 0.5f)
    {
        float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
        result += vec3(0.32f, 0.42f, 0.62f) * rim * uRimAmt;
        result += vec3(0.35f, 0.50f, 0.80f) * rim * uRimAmt;
        // 脸部补光：面向相机方向提亮，抹平生硬阴影
        if (uSoftShade > 0)
            result += baseColor * max(dot(N, V), 0.0) * uFaceFill * 0.30f;
    }
    FragColor = vec4(result, 1.0);
}
)";

static const char*lineVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vColor;
void main()
{
    vColor = aColor;
    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
}
)";

static const char*lineFragSrc = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main()
{
    FragColor = vec4(vColor, 1.0);
}
)";

static const char*skyVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uProj;
uniform mat4 uView;
out vec3 vDir;
void main()
{
    vDir = aPos;
    mat4 viewRot = mat4(mat3(uView));
    vec4 pos = uProj * viewRot * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
}
)";

static const char*skyFragSrc = R"(
#version 330 core
in vec3 vDir;
out vec4 FragColor;
uniform vec3 uSunDir;
void main()
{
    vec3 dir = normalize(vDir);
    float h = clamp(dir.y, -1.0, 1.0);
    vec3 zenith  = vec3(0.10f, 0.26f, 0.52f);
    vec3 horizon = vec3(0.68f, 0.80f, 0.88f);
    vec3 ground  = vec3(0.15f, 0.15f, 0.17f);
    vec3 col;
    if (dir.y < 0.0)
        col = mix(ground, horizon, smoothstep(-0.25f, 0.0f, h));
    else
        col = mix(horizon, zenith, pow(h, 0.45f));
    float sun = max(dot(dir, normalize(uSunDir)), 0.0);
    col += vec3(1.0f, 0.95f, 0.82f) * (pow(sun, 800.0f) * 1.4f + pow(sun, 24.0f) * 0.30f);
    FragColor = vec4(col, 1.0);
}
)";

static const char*flatVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uMvp;
void main()
{
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

static const char*flatFragSrc = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main()
{
    FragColor = vec4(uColor, 1.0);
}
)";

// 屏幕空间描边后处理（全屏三角形）
static const char*postVertSrc = R"(
#version 330 core
out vec2 vUv;
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

static const char*postFragSrc = R"(
#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform float uOutlineOn;
uniform float uGradeOn;
uniform float uSatAmt;
uniform float uTime;
uniform float uBloomAmt;
uniform float uGrainAmt;

float toLinear(float z)
{
    const float n = 0.05;
    const float f = 500.0;
    return z;   // 原始非线性深度：远处深度差小，避免远处整片被当描边涂黑
}

void main()
{
    vec3 col = texture(uScene, vUv).rgb;
    if (uOutlineOn > 0.5)
    {
        vec2 texel = 1.0 / vec2(textureSize(uDepth, 0));
        float c = toLinear(texture(uDepth, vUv).r);
        float l = toLinear(texture(uDepth, vUv - vec2(texel.x, 0.0)).r);
        float r = toLinear(texture(uDepth, vUv + vec2(texel.x, 0.0)).r);
        float u = toLinear(texture(uDepth, vUv + vec2(0.0, texel.y)).r);
        float d = toLinear(texture(uDepth, vUv - vec2(0.0, texel.y)).r);
        float mag = abs(l - r) + abs(u - d);
        float edgeD = smoothstep(0.004, 0.02, mag);   // 非线性深度阈值
        vec3 nC = texture(uNormal, vUv).rgb * 2.0 - 1.0;
        vec3 nL = texture(uNormal, vUv - vec2(texel.x, 0.0)).rgb * 2.0 - 1.0;
        vec3 nR = texture(uNormal, vUv + vec2(texel.x, 0.0)).rgb * 2.0 - 1.0;
        vec3 nU = texture(uNormal, vUv + vec2(0.0, texel.y)).rgb * 2.0 - 1.0;
        vec3 nD = texture(uNormal, vUv - vec2(0.0, texel.y)).rgb * 2.0 - 1.0;
        float nl = (c < 0.995f && l < 0.995f) ? 1.0f : 0.0f;   // 左右两侧都是几何体才允许法线折痕
        float nu = (c < 0.995f && u < 0.995f) ? 1.0f : 0.0f;
        float magN = length(nR - nL) * nl + length(nU - nD) * nu;
        float edgeN = smoothstep(0.18, 0.60, magN);
        float edge = max(edgeD, edgeN * 0.45);
        float distFade = 1.0f - smoothstep(0.55f, 0.92f, c);   // 距离淡出描边
        col = mix(col, vec3(0.08, 0.09, 0.16), edge * 0.50f * distFade);   // 深蓝灰描边
    }
    if (uGradeOn > 0.5)
    {
        float luma = dot(col, vec3(0.299, 0.587, 0.114));
        col = mix(vec3(luma), col, uSatAmt);
        col = col * 0.97 + 0.028;
        col *= vec3(1.015f, 1.0f, 0.965f);   // 轻微暖调，统一画面

        vec2 gtex = 1.0 / vec2(textureSize(uDepth, 0));
        float gc = texture(uDepth, vUv).r;
        float gl = texture(uDepth, vUv - vec2(gtex.x, 0.0)).r;
        float gr = texture(uDepth, vUv + vec2(gtex.x, 0.0)).r;
        float gu = texture(uDepth, vUv + vec2(0.0, gtex.y)).r;
        float gd = texture(uDepth, vUv - vec2(0.0, gtex.y)).r;
        float lap = abs(2.0 * gc - gl - gr) + abs(2.0 * gc - gu - gd);
        col *= 1.0 - smoothstep(0.003, 0.025, lap) * 0.22;   // 凹处伪AO

        // 柔和泛光（4 方向采样，展开写避免解析问题）
        vec2 d1 = vec2(gtex.x * 2.0, 0.0);
        vec2 d2 = vec2(0.0, gtex.y * 2.0);
        vec3 glow = vec3(0.0);
        glow += max(texture(uScene, vUv - d1).rgb - vec3(0.85), vec3(0.0));
        glow += max(texture(uScene, vUv + d1).rgb - vec3(0.85), vec3(0.0));
        glow += max(texture(uScene, vUv - d2).rgb - vec3(0.85), vec3(0.0));
        glow += max(texture(uScene, vUv + d2).rgb - vec3(0.85), vec3(0.0));
        col += (glow * 0.25) * uBloomAmt;
        // 噪点颗粒
        float nh = fract(sin(dot(vUv * 100.0 + uTime, vec2(12.9898, 78.233))) * 43758.5453);
        col += (nh - 0.5) * uGrainAmt;
        vec2 q = vUv - 0.5;
        float vig = smoothstep(0.45, 1.15, length(q) * 1.45);
        col *= 1.0 - vig * 0.38;   // 暗角
    }
    FragColor = vec4(col, 1.0);
}
)";

// 法线通道：把视空间法线写入场景 FBO 的 COLOR_ATTACHMENT1
static const char*normalVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMat;
out vec3 vViewNormal;
void main()
{
    vec4 world = uModel * vec4(aPos, 1.0);
    vViewNormal = mat3(uView) * uNormalMat * aNormal;
    gl_Position = uProj * uView * world;
}
)";

static const char*normalFragSrc = R"(
#version 330 core
in vec3 vViewNormal;
out vec4 FragColor;
void main()
{
    vec3 n = normalize(vViewNormal);
    FragColor = vec4(n * 0.5 + 0.5, 1.0);
}
)";
