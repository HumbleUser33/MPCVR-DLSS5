// MIT License
//
// Pass 7 of ArtCNN_C4F16_DS.glsl (ArtCNN C4F16 DS (Depth-To-Space)), translated to HLSL by Shaders/mpv/mpv_shaders.py
// (glslang, then SPIRV-Cross): do not edit.

static const uint3 gl_WorkGroupSize = uint3(12u, 16u, 1u);

cbuffer MpvGlobals : register(b0)
{
    float4 _35_pl_out_size : packoffset(c0);
    float4 _35_pl_input_target : packoffset(c1);
    float4 _35_pl_misc : packoffset(c2);
    float4 _35_pl_tex_size[8] : packoffset(c3);
    float4 _35_pl_tex_shift[8] : packoffset(c11);
    float4 _35_pl_params[8] : packoffset(c19);
};

Texture2D<float4> conv2d_6_raw : register(t1);
SamplerState _conv2d_6_raw_sampler : register(s1);
RWTexture2D<float4> out_image : register(u0);

static uint3 gl_GlobalInvocationID;
struct SPIRV_Cross_Input
{
    uint3 gl_GlobalInvocationID : SV_DispatchThreadID;
};

void hook()
{
    float4 result = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float2 f0 = frac((((float2(gl_GlobalInvocationID.xy) + 0.5f.xx) * _35_pl_out_size.zw) + _35_pl_tex_shift[0].xy) * _35_pl_tex_size[0].xy);
    int2 i0 = int2(f0 * 2.0f.xx);
    result.x = (conv2d_6_raw.SampleLevel(_conv2d_6_raw_sampler, ((0.5f.xx - f0) * _35_pl_tex_size[0].zw) + (((float2(gl_GlobalInvocationID.xy) + 0.5f.xx) * _35_pl_out_size.zw) + _35_pl_tex_shift[0].xy), 0.0f) * 1.0f)[(i0.y * 2) + i0.x];
    out_image[int2(int3(gl_GlobalInvocationID).xy)] = clamp(result, 0.0f.xxxx, 1.0f.xxxx);
}

void comp_main()
{
    hook();
}

[numthreads(12, 16, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
