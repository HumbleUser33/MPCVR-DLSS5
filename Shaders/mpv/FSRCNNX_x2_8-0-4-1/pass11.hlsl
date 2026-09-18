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
// Pass 11 of FSRCNNX_x2_8-0-4-1.glsl (sub-band residuals 2), translated to HLSL by Shaders/mpv/mpv_shaders.py
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
Texture2D<float4> FEATURE2_raw : register(t3);
SamplerState _FEATURE2_raw_sampler : register(s3);

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
    float4 res = float4(-0.0425243787467479705810546875f, -0.3715015351772308349609375f, -0.025622785091400146484375f, -0.2774516046047210693359375f);
    res += mul(MODEL1_raw.SampleLevel(_MODEL1_raw_sampler, (pl_pos + _57_pl_tex_shift[0].xy) + (_57_pl_tex_size[0].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(0.023811884224414825439453125f, 0.02954806573688983917236328125f, -0.0066418983042240142822265625f, 0.1021223962306976318359375f), float4(-0.0568209178745746612548828125f, -0.43551003932952880859375f, -0.270052254199981689453125f, -0.2060186564922332763671875f), float4(-0.06896133720874786376953125f, -0.16896919906139373779296875f, -0.03067485056817531585693359375f, -0.24612522125244140625f), float4(-0.0057375836186110973358154296875f, -0.18923032283782958984375f, -0.02858714945614337921142578125f, -0.5032613277435302734375f)));
    res += mul(MODEL2_raw.SampleLevel(_MODEL2_raw_sampler, (pl_pos + _57_pl_tex_shift[1].xy) + (_57_pl_tex_size[1].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(0.546321332454681396484375f, 0.097280032932758331298828125f, 0.03075607679784297943115234375f, 0.067805893719196319580078125f), float4(-0.035606302320957183837890625f, -0.701386511325836181640625f, 0.18904435634613037109375f, -0.103665746748447418212890625f), float4(-0.17458261549472808837890625f, -0.2942218780517578125f, -0.0485423319041728973388671875f, -0.2983124554157257080078125f), float4(-0.052443183958530426025390625f, -0.3261034786701202392578125f, 0.3217246532440185546875f, 0.19580185413360595703125f)));
    res += (FEATURE2_raw.SampleLevel(_FEATURE2_raw_sampler, (pl_pos + _57_pl_tex_shift[2].xy) + (_57_pl_tex_size[2].zw * 0.0f.xx), 0.0f) * 1.0f);
    res = max(res, 0.0f.xxxx) + (float4(0.139133989810943603515625f, 0.096032835543155670166015625f, 0.623534142971038818359375f, 0.1177272796630859375f) * min(res, 0.0f.xxxx));
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
