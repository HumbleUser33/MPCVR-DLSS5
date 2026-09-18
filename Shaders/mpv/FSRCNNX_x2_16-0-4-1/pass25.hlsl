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
// Pass 25 of FSRCNNX_x2_16-0-4-1.glsl (aggregation), translated to HLSL by Shaders/mpv/mpv_shaders.py
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

Texture2D<float4> SUBCONV1_raw : register(t1);
SamplerState _SUBCONV1_raw_sampler : register(s1);

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
    float2 fcoord = frac((pl_pos + _24_pl_tex_shift[0].xy) * _24_pl_tex_size[0].xy);
    float2 base = (pl_pos + _24_pl_tex_shift[0].xy) + ((0.5f.xx - fcoord) * _24_pl_tex_size[0].zw);
    int2 index = int2(fcoord * 2.0f.xx);
    float4 res = SUBCONV1_raw.SampleLevel(_SUBCONV1_raw_sampler, base, 0.0f) * 1.0f;
    return float4(res[(index.x * 2) + index.y], 0.0f, 0.0f, 1.0f);
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
