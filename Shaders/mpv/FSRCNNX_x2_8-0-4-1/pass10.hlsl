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
// Pass 10 of FSRCNNX_x2_8-0-4-1.glsl (sub-band residuals 1), translated to HLSL by Shaders/mpv/mpv_shaders.py
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
Texture2D<float4> FEATURE1_raw : register(t3);
SamplerState _FEATURE1_raw_sampler : register(s3);

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
    float4 res = float4(0.024564854800701141357421875f, -0.446778476238250732421875f, 0.01975269429385662078857421875f, -0.011000041849911212921142578125f);
    res += mul(MODEL1_raw.SampleLevel(_MODEL1_raw_sampler, (pl_pos + _57_pl_tex_shift[0].xy) + (_57_pl_tex_size[0].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(0.030266530811786651611328125f, -0.926222145557403564453125f, -0.116113476455211639404296875f, -0.050690047442913055419921875f), float4(0.2716045379638671875f, -0.04858715832233428955078125f, 0.0044713355600833892822265625f, -0.4274623394012451171875f), float4(0.0749531090259552001953125f, -0.3700785338878631591796875f, 0.0350039415061473846435546875f, -0.05407865345478057861328125f), float4(-0.060739003121852874755859375f, -0.801990032196044921875f, 0.0923245251178741455078125f, 0.1258827745914459228515625f)));
    res += mul(MODEL2_raw.SampleLevel(_MODEL2_raw_sampler, (pl_pos + _57_pl_tex_shift[1].xy) + (_57_pl_tex_size[1].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(-0.06491352617740631103515625f, 0.081523679196834564208984375f, 0.006733429618179798126220703125f, 0.127742588520050048828125f), float4(-0.0051357815973460674285888671875f, -0.1485908329486846923828125f, 0.0074226572178304195404052734375f, 0.005062350071966648101806640625f), float4(0.0588018335402011871337890625f, -0.069255292415618896484375f, 0.12887252867221832275390625f, -0.09893862903118133544921875f), float4(0.042793683707714080810546875f, 0.0967708528041839599609375f, -0.045563213527202606201171875f, -0.0711275041103363037109375f)));
    res += (FEATURE1_raw.SampleLevel(_FEATURE1_raw_sampler, (pl_pos + _57_pl_tex_shift[2].xy) + (_57_pl_tex_size[2].zw * 0.0f.xx), 0.0f) * 1.0f);
    res = max(res, 0.0f.xxxx) + (float4(0.99271869659423828125f, 0.0570580027997493743896484375f, 1.322675228118896484375f, 1.00694668292999267578125f) * min(res, 0.0f.xxxx));
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
