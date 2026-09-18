// Copyright (C) 2017-2021 igv
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 3.0 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// Pass 20 of FSRCNNX_x2_16-0-4-1.glsl (sub-band residuals 1), translated to HLSL by Shaders/mpv/mpv_shaders.py
// (glslang, then SPIRV-Cross): do not edit.

cbuffer MpvGlobals : register(b0)
{
    float4 _57_pl_out_size : packoffset(c0);
    float4 _57_pl_input_target : packoffset(c1);
    float4 _57_pl_misc : packoffset(c2);
    float4 _57_pl_tex_size[8] : packoffset(c3);
    float4 _57_pl_tex_shift[8] : packoffset(c11);
    float4 _57_pl_params[8] : packoffset(c19);
};

Texture2D<float4> MODEL1_raw : register(t1);
SamplerState _MODEL1_raw_sampler : register(s1);
Texture2D<float4> MODEL2_raw : register(t2);
SamplerState _MODEL2_raw_sampler : register(s2);
Texture2D<float4> MODEL3_raw : register(t3);
SamplerState _MODEL3_raw_sampler : register(s3);
Texture2D<float4> MODEL4_raw : register(t4);
SamplerState _MODEL4_raw_sampler : register(s4);
Texture2D<float4> FEATURE1_raw : register(t5);
SamplerState _FEATURE1_raw_sampler : register(s5);

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

float4 hook()
{
    float4 res = float4(-0.02188730053603649139404296875f, -0.014957615174353122711181640625f, -0.02561801858246326446533203125f, -0.0858701169490814208984375f);
    res += mul(MODEL1_raw.SampleLevel(_MODEL1_raw_sampler, (pl_pos + _57_pl_tex_shift[0].xy) + (_57_pl_tex_size[0].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(0.02216815389692783355712890625f, -0.2356450855731964111328125f, 0.0643408298492431640625f, -1.11380326747894287109375f), float4(0.10646246373653411865234375f, -0.891471564769744873046875f, -0.02941730059683322906494140625f, -0.00226535671390593051910400390625f), float4(-0.025269933044910430908203125f, -0.123033307492733001708984375f, -0.08582450449466705322265625f, 0.0581396929919719696044921875f), float4(0.01812312938272953033447265625f, 0.111153699457645416259765625f, 0.093519397079944610595703125f, -0.074283547699451446533203125f)));
    res += mul(MODEL2_raw.SampleLevel(_MODEL2_raw_sampler, (pl_pos + _57_pl_tex_shift[1].xy) + (_57_pl_tex_size[1].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(0.073628313839435577392578125f, 0.014797971583902835845947265625f, 0.0464740432798862457275390625f, -0.214890003204345703125f), float4(0.4483453333377838134765625f, 0.2830028533935546875f, 0.056207694113254547119140625f, 0.3613960742950439453125f), float4(0.066843353211879730224609375f, -0.269219934940338134765625f, 0.0970326364040374755859375f, 0.124850757420063018798828125f), float4(0.020818032324314117431640625f, 0.0532030761241912841796875f, -0.00983686745166778564453125f, 0.049896739423274993896484375f)));
    res += mul(MODEL3_raw.SampleLevel(_MODEL3_raw_sampler, (pl_pos + _57_pl_tex_shift[2].xy) + (_57_pl_tex_size[2].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(-0.0427469126880168914794921875f, 0.1918861865997314453125f, -0.046821437776088714599609375f, -0.11546061933040618896484375f), float4(-0.801821231842041015625f, 0.394140064716339111328125f, 0.0562537945806980133056640625f, 0.24593056738376617431640625f), float4(-0.00345763005316257476806640625f, -0.40446054935455322265625f, 0.0631769001483917236328125f, 0.2570861279964447021484375f), float4(0.3880051076412200927734375f, 0.21024425327777862548828125f, 0.103503160178661346435546875f, -0.18450926244258880615234375f)));
    res += mul(MODEL4_raw.SampleLevel(_MODEL4_raw_sampler, (pl_pos + _57_pl_tex_shift[3].xy) + (_57_pl_tex_size[3].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(-0.073650456964969635009765625f, -0.43291342258453369140625f, -0.03318835794925689697265625f, 0.4859155714511871337890625f), float4(0.03006631694734096527099609375f, -0.14314131438732147216796875f, 0.090068928897380828857421875f, 0.00275098369456827640533447265625f), float4(-0.0178727619349956512451171875f, -0.103744857013225555419921875f, -0.042906261980533599853515625f, 0.010455760173499584197998046875f), float4(-0.078748472034931182861328125f, 0.033428095281124114990234375f, -0.02108508162200450897216796875f, -0.2790249288082122802734375f)));
    res += (FEATURE1_raw.SampleLevel(_FEATURE1_raw_sampler, (pl_pos + _57_pl_tex_shift[4].xy) + (_57_pl_tex_size[4].zw * 0.0f.xx), 0.0f) * 1.0f);
    res = max(res, 0.0f.xxxx) + (float4(-0.000942182727158069610595703125f, 0.03723652660846710205078125f, 0.54574549198150634765625f, -0.00455802865326404571533203125f) * min(res, 0.0f.xxxx));
    return res;
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
