/* grain.hlsl - 自定义 D2D 效果: 预乘 alpha 的细颗粒
 *
 * D2D 会给像素着色器三个固定参数:
 *   SV_POSITION     clip-space 输出
 *   SCENE_POSITION  目标表面像素坐标 (用它做稳定噪点)
 *   TEXCOORD0       输入纹理的 texel 坐标
 * 编译: fxc /T ps_4_0 /E main /Fh build\grain_cso.h /Vn g_grainCso
 */
Texture2D InputTexture : register(t0);
SamplerState InputSampler : register(s0);

cbuffer GrainConstants : register(b0)
{
    float intensity : packoffset(c0.x);
    float3 pad : packoffset(c0.y);
};

float hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float4 main(float4 clip : SV_POSITION,
            float4 scene : SCENE_POSITION,
            float4 tex : TEXCOORD0) : SV_TARGET
{
    float4 c = InputTexture.Sample(InputSampler, tex.xy);
    float n = hash21(scene.xy);
    c.rgb = saturate(c.rgb + (n - 0.5) * intensity * c.a);
    return c;
}
