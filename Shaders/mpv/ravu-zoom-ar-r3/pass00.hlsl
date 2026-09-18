//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// Pass 0 of ravu-zoom-ar-r3.hook (RAVU-Zoom-AR (luma, r3)), translated to HLSL by Shaders/mpv/mpv_shaders.py
// (glslang, then SPIRV-Cross): do not edit.

cbuffer MpvGlobals : register(b0)
{
    float4 _24_pl_out_size : packoffset(c0);
    float4 _24_pl_input_target : packoffset(c1);
    float4 _24_pl_misc : packoffset(c2);
    float4 _24_pl_tex_size[8] : packoffset(c3);
    float4 _24_pl_tex_shift[8] : packoffset(c11);
    float4 _24_pl_params[8] : packoffset(c19);
};

Texture2D<float4> HOOKED_raw : register(t1);
SamplerState _HOOKED_raw_sampler : register(s1);
Texture2D<float4> ravu_zoom_lut3_raw : register(t2);
SamplerState _ravu_zoom_lut3_raw_sampler : register(s2);
Texture2D<float4> ravu_zoom_lut3_ar_raw : register(t3);
SamplerState _ravu_zoom_lut3_ar_raw_sampler : register(s3);

static float2 pl_pos;
static float4 pl_out;

struct SPIRV_Cross_Input
{
    float2 pl_pos : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 pl_out : SV_Target0;
};

float mod(float x, float y)
{
    return x - y * floor(x / y);
}

float2 mod(float2 x, float2 y)
{
    return x - y * floor(x / y);
}

float3 mod(float3 x, float3 y)
{
    return x - y * floor(x / y);
}

float4 mod(float4 x, float4 y)
{
    return x - y * floor(x / y);
}

float4 hook()
{
    float2 pos = (pl_pos + _24_pl_tex_shift[0].xy) * _24_pl_tex_size[0].xy;
    float2 subpix = frac(pos - 0.5f.xx);
    pos -= subpix;
    subpix = lerp(0.0555555559694766998291015625f.xx, 0.9444444179534912109375f.xx, subpix);
    float2 subpix_inv = 1.0f.xx - subpix;
    float2 subpix_ar = subpix / float2(2.0f, 288.0f);
    float2 subpix_inv_ar = subpix_inv / float2(2.0f, 288.0f);
    subpix /= float2(5.0f, 288.0f);
    subpix_inv /= float2(5.0f, 288.0f);
    float4 gather0 = HOOKED_raw.GatherRed(_HOOKED_raw_sampler, pos * _24_pl_tex_size[0].zw, int2(-2, -2)) * 1.0f;
    float4 gather2 = HOOKED_raw.GatherRed(_HOOKED_raw_sampler, pos * _24_pl_tex_size[0].zw, int2(-2, 0)) * 1.0f;
    float4 gather4 = HOOKED_raw.GatherRed(_HOOKED_raw_sampler, pos * _24_pl_tex_size[0].zw, int2(-2, 2)) * 1.0f;
    float4 gather12 = HOOKED_raw.GatherRed(_HOOKED_raw_sampler, pos * _24_pl_tex_size[0].zw, int2(0, -2)) * 1.0f;
    float4 gather14 = HOOKED_raw.GatherRed(_HOOKED_raw_sampler, pos * _24_pl_tex_size[0].zw, int2(0, 0)) * 1.0f;
    float4 gather16 = HOOKED_raw.GatherRed(_HOOKED_raw_sampler, pos * _24_pl_tex_size[0].zw, int2(0, 2)) * 1.0f;
    float4 gather24 = HOOKED_raw.GatherRed(_HOOKED_raw_sampler, pos * _24_pl_tex_size[0].zw, int2(2, -2)) * 1.0f;
    float4 gather26 = HOOKED_raw.GatherRed(_HOOKED_raw_sampler, pos * _24_pl_tex_size[0].zw, int2(2, 0)) * 1.0f;
    float4 gather28 = HOOKED_raw.GatherRed(_HOOKED_raw_sampler, pos * _24_pl_tex_size[0].zw, int2(2, 2)) * 1.0f;
    float3 abd = 0.0f.xxx;
    float gx = (gather12.x - gather0.x) / 2.0f;
    float gy = (gather2.z - gather0.z) / 2.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.0479223541915416717529296875f);
    gx = (gather14.w - gather2.w) / 2.0f;
    gy = ((((-gather4.z) + (8.0f * gather2.y)) - (8.0f * gather0.y)) + gather0.z) / 12.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.0615335218608379364013671875f);
    gx = (gather14.x - gather2.x) / 2.0f;
    gy = ((((-gather4.y) + (8.0f * gather4.z)) - (8.0f * gather2.z)) + gather0.y) / 12.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.0615335218608379364013671875f);
    gx = (gather16.w - gather4.w) / 2.0f;
    gy = (gather4.y - gather2.y) / 2.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.0479223541915416717529296875f);
    gx = ((((-gather24.x) + (8.0f * gather12.y)) - (8.0f * gather0.y)) + gather0.x) / 12.0f;
    gy = (gather14.w - gather12.w) / 2.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.0615335218608379364013671875f);
    gx = ((((-gather26.w) + (8.0f * gather14.z)) - (8.0f * gather2.z)) + gather2.w) / 12.0f;
    gy = ((((-gather16.w) + (8.0f * gather14.x)) - (8.0f * gather12.x)) + gather12.w) / 12.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.07901060581207275390625f);
    gx = ((((-gather26.x) + (8.0f * gather14.y)) - (8.0f * gather2.y)) + gather2.x) / 12.0f;
    gy = ((((-gather16.x) + (8.0f * gather16.w)) - (8.0f * gather14.w)) + gather12.x) / 12.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.07901060581207275390625f);
    gx = ((((-gather28.w) + (8.0f * gather16.z)) - (8.0f * gather4.z)) + gather4.w) / 12.0f;
    gy = (gather16.x - gather14.x) / 2.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.0615335218608379364013671875f);
    gx = ((((-gather24.y) + (8.0f * gather24.x)) - (8.0f * gather12.x)) + gather0.y) / 12.0f;
    gy = (gather14.z - gather12.z) / 2.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.0615335218608379364013671875f);
    gx = ((((-gather26.z) + (8.0f * gather26.w)) - (8.0f * gather14.w)) + gather2.z) / 12.0f;
    gy = ((((-gather16.z) + (8.0f * gather14.y)) - (8.0f * gather12.y)) + gather12.z) / 12.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.07901060581207275390625f);
    gx = ((((-gather26.y) + (8.0f * gather26.x)) - (8.0f * gather14.x)) + gather2.y) / 12.0f;
    gy = ((((-gather16.y) + (8.0f * gather16.z)) - (8.0f * gather14.z)) + gather12.y) / 12.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.07901060581207275390625f);
    gx = ((((-gather28.z) + (8.0f * gather28.w)) - (8.0f * gather16.w)) + gather4.z) / 12.0f;
    gy = (gather16.y - gather14.y) / 2.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.0615335218608379364013671875f);
    gx = (gather24.y - gather12.y) / 2.0f;
    gy = (gather26.w - gather24.w) / 2.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.0479223541915416717529296875f);
    gx = (gather26.z - gather14.z) / 2.0f;
    gy = ((((-gather28.w) + (8.0f * gather26.x)) - (8.0f * gather24.x)) + gather24.w) / 12.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.0615335218608379364013671875f);
    gx = (gather26.y - gather14.y) / 2.0f;
    gy = ((((-gather28.x) + (8.0f * gather28.w)) - (8.0f * gather26.w)) + gather24.x) / 12.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.0615335218608379364013671875f);
    gx = (gather28.z - gather16.z) / 2.0f;
    gy = (gather28.x - gather26.x) / 2.0f;
    abd += (float3(gx * gx, gx * gy, gy * gy) * 0.0479223541915416717529296875f);
    float a = abd.x;
    float b = abd.y;
    float d = abd.z;
    float T = a + d;
    float D = (a * d) - (b * b);
    float delta = sqrt(max(((T * T) / 4.0f) - D, 0.0f));
    float L1 = (T / 2.0f) + delta;
    float L2 = (T / 2.0f) - delta;
    float sqrtL1 = sqrt(L1);
    float sqrtL2 = sqrt(L2);
    float theta = (abs(b) < 1.1920928955078125e-07f) ? 0.0f : mod(atan2(L1 - a, b) + 3.1415927410125732421875f, 3.1415927410125732421875f);
    float lambda = sqrtL1;
    float mu = ((sqrtL1 + sqrtL2) < 1.1920928955078125e-07f) ? 0.0f : ((sqrtL1 - sqrtL2) / (sqrtL1 + sqrtL2));
    float angle = floor((theta * 24.0f) / 3.1415927410125732421875f);
    float strength = (lambda >= 0.01600000075995922088623046875f) ? ((lambda >= 0.0500000007450580596923828125f) ? 3.0f : 2.0f) : float(lambda >= 0.0040000001899898052215576171875f);
    float coherence = (mu >= 0.5f) ? 2.0f : float(mu >= 0.25f);
    float coord_y = ((((angle * 4.0f) + strength) * 3.0f) + coherence) / 288.0f;
    float res = 0.0f;
    float lo = 0.0f;
    float hi = 0.0f;
    float lo2 = 0.0f;
    float hi2 = 0.0f;
    float4 w = ravu_zoom_lut3_raw.Sample(_ravu_zoom_lut3_raw_sampler, float2(0.0f, coord_y) + subpix);
    res += (gather0.w * w.x);
    res += (gather0.x * w.y);
    res += (gather2.w * w.z);
    res += (gather2.x * w.w);
    w = ravu_zoom_lut3_raw.Sample(_ravu_zoom_lut3_raw_sampler, float2(0.20000000298023223876953125f, coord_y) + subpix);
    res += (gather4.w * w.x);
    res += (gather4.x * w.y);
    res += (gather0.z * w.z);
    res += (gather0.y * w.w);
    w = ravu_zoom_lut3_raw.Sample(_ravu_zoom_lut3_raw_sampler, float2(0.4000000059604644775390625f, coord_y) + subpix);
    res += (gather2.z * w.x);
    res += (gather2.y * w.y);
    res += (gather4.z * w.z);
    res += (gather4.y * w.w);
    w = ravu_zoom_lut3_raw.Sample(_ravu_zoom_lut3_raw_sampler, float2(0.60000002384185791015625f, coord_y) + subpix);
    res += (gather12.w * w.x);
    res += (gather12.x * w.y);
    res += (gather14.w * w.z);
    res += (gather14.x * w.w);
    w = ravu_zoom_lut3_raw.Sample(_ravu_zoom_lut3_raw_sampler, float2(0.800000011920928955078125f, coord_y) + subpix);
    res += (gather16.w * w.x);
    res += (gather16.x * w.y);
    w = ravu_zoom_lut3_raw.Sample(_ravu_zoom_lut3_raw_sampler, float2(0.0f, coord_y) + subpix_inv);
    res += (gather28.y * w.x);
    res += (gather28.z * w.y);
    res += (gather26.y * w.z);
    res += (gather26.z * w.w);
    w = ravu_zoom_lut3_raw.Sample(_ravu_zoom_lut3_raw_sampler, float2(0.20000000298023223876953125f, coord_y) + subpix_inv);
    res += (gather24.y * w.x);
    res += (gather24.z * w.y);
    res += (gather28.x * w.z);
    res += (gather28.w * w.w);
    w = ravu_zoom_lut3_raw.Sample(_ravu_zoom_lut3_raw_sampler, float2(0.4000000059604644775390625f, coord_y) + subpix_inv);
    res += (gather26.x * w.x);
    res += (gather26.w * w.y);
    res += (gather24.x * w.z);
    res += (gather24.w * w.w);
    w = ravu_zoom_lut3_raw.Sample(_ravu_zoom_lut3_raw_sampler, float2(0.60000002384185791015625f, coord_y) + subpix_inv);
    res += (gather16.y * w.x);
    res += (gather16.z * w.y);
    res += (gather14.y * w.z);
    res += (gather14.z * w.w);
    w = ravu_zoom_lut3_raw.Sample(_ravu_zoom_lut3_raw_sampler, float2(0.800000011920928955078125f, coord_y) + subpix_inv);
    res += (gather12.y * w.x);
    res += (gather12.z * w.y);
    w = ravu_zoom_lut3_ar_raw.Sample(_ravu_zoom_lut3_ar_raw_sampler, float2(0.0f, coord_y) + subpix_ar);
    float4 cg = float4(0.100000001490116119384765625f + gather0.y, 1.10000002384185791015625f - gather0.y, 0.100000001490116119384765625f + gather2.z, 1.10000002384185791015625f - gather2.z);
    float4 cg1 = cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    hi += ((cg.x * w.x) + (cg.z * w.y));
    lo += ((cg.y * w.x) + (cg.w * w.y));
    cg *= cg1;
    hi2 += ((cg.x * w.x) + (cg.z * w.y));
    lo2 += ((cg.y * w.x) + (cg.w * w.y));
    cg = float4(0.100000001490116119384765625f + gather2.y, 1.10000002384185791015625f - gather2.y, 0.100000001490116119384765625f + gather4.z, 1.10000002384185791015625f - gather4.z);
    cg1 = cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    hi += ((cg.x * w.z) + (cg.z * w.w));
    lo += ((cg.y * w.z) + (cg.w * w.w));
    cg *= cg1;
    hi2 += ((cg.x * w.z) + (cg.z * w.w));
    lo2 += ((cg.y * w.z) + (cg.w * w.w));
    w = ravu_zoom_lut3_ar_raw.Sample(_ravu_zoom_lut3_ar_raw_sampler, float2(0.5f, coord_y) + subpix_ar);
    cg = float4(0.100000001490116119384765625f + gather12.x, 1.10000002384185791015625f - gather12.x, 0.100000001490116119384765625f + gather14.w, 1.10000002384185791015625f - gather14.w);
    cg1 = cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    hi += ((cg.x * w.x) + (cg.z * w.y));
    lo += ((cg.y * w.x) + (cg.w * w.y));
    cg *= cg1;
    hi2 += ((cg.x * w.x) + (cg.z * w.y));
    lo2 += ((cg.y * w.x) + (cg.w * w.y));
    cg = float4(0.100000001490116119384765625f + gather14.x, 1.10000002384185791015625f - gather14.x, 0.100000001490116119384765625f + gather16.w, 1.10000002384185791015625f - gather16.w);
    cg1 = cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    hi += ((cg.x * w.z) + (cg.z * w.w));
    lo += ((cg.y * w.z) + (cg.w * w.w));
    cg *= cg1;
    hi2 += ((cg.x * w.z) + (cg.z * w.w));
    lo2 += ((cg.y * w.z) + (cg.w * w.w));
    w = ravu_zoom_lut3_ar_raw.Sample(_ravu_zoom_lut3_ar_raw_sampler, float2(0.0f, coord_y) + subpix_inv_ar);
    cg = float4(0.100000001490116119384765625f + gather28.w, 1.10000002384185791015625f - gather28.w, 0.100000001490116119384765625f + gather26.x, 1.10000002384185791015625f - gather26.x);
    cg1 = cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    hi += ((cg.x * w.x) + (cg.z * w.y));
    lo += ((cg.y * w.x) + (cg.w * w.y));
    cg *= cg1;
    hi2 += ((cg.x * w.x) + (cg.z * w.y));
    lo2 += ((cg.y * w.x) + (cg.w * w.y));
    cg = float4(0.100000001490116119384765625f + gather26.w, 1.10000002384185791015625f - gather26.w, 0.100000001490116119384765625f + gather24.x, 1.10000002384185791015625f - gather24.x);
    cg1 = cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    hi += ((cg.x * w.z) + (cg.z * w.w));
    lo += ((cg.y * w.z) + (cg.w * w.w));
    cg *= cg1;
    hi2 += ((cg.x * w.z) + (cg.z * w.w));
    lo2 += ((cg.y * w.z) + (cg.w * w.w));
    w = ravu_zoom_lut3_ar_raw.Sample(_ravu_zoom_lut3_ar_raw_sampler, float2(0.5f, coord_y) + subpix_inv_ar);
    cg = float4(0.100000001490116119384765625f + gather16.z, 1.10000002384185791015625f - gather16.z, 0.100000001490116119384765625f + gather14.y, 1.10000002384185791015625f - gather14.y);
    cg1 = cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    hi += ((cg.x * w.x) + (cg.z * w.y));
    lo += ((cg.y * w.x) + (cg.w * w.y));
    cg *= cg1;
    hi2 += ((cg.x * w.x) + (cg.z * w.y));
    lo2 += ((cg.y * w.x) + (cg.w * w.y));
    cg = float4(0.100000001490116119384765625f + gather14.z, 1.10000002384185791015625f - gather14.z, 0.100000001490116119384765625f + gather12.y, 1.10000002384185791015625f - gather12.y);
    cg1 = cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    cg *= cg;
    hi += ((cg.x * w.z) + (cg.z * w.w));
    lo += ((cg.y * w.z) + (cg.w * w.w));
    cg *= cg1;
    hi2 += ((cg.x * w.z) + (cg.z * w.w));
    lo2 += ((cg.y * w.z) + (cg.w * w.w));
    hi = (hi2 / hi) - 0.100000001490116119384765625f;
    lo = 1.10000002384185791015625f - (lo2 / lo);
    res = lerp(res, clamp(res, lo, hi), 0.800000011920928955078125f);
    return float4(res, 0.0f, 0.0f, 0.0f);
}

void frag_main()
{
    pl_out = hook();
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    pl_pos = stage_input.pl_pos;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.pl_out = pl_out;
    return stage_output;
}
