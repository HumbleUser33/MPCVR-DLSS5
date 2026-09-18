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
// Pass 0 of FSRCNNX_x2_16-0-4-1.glsl (feature map 1), translated to HLSL by Shaders/mpv/mpv_shaders.py
// (glslang, then SPIRV-Cross): do not edit.

cbuffer MpvGlobals : register(b0)
{
    float4 _40_pl_out_size : packoffset(c0);
    float4 _40_pl_input_target : packoffset(c1);
    float4 _40_pl_misc : packoffset(c2);
    float4 _40_pl_tex_size[8] : packoffset(c3);
    float4 _40_pl_tex_shift[8] : packoffset(c11);
    float4 _40_pl_params[8] : packoffset(c19);
};

Texture2D<float4> LUMA_raw : register(t1);
SamplerState _LUMA_raw_sampler : register(s1);

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
    float4 res = float4(-0.015858300030231475830078125f, 0.0481607876718044281005859375f, 0.02167440019547939300537109375f, 0.03275175392627716064453125f);
    res += (float4(0.0055716447532176971435546875f, -0.0011689565144479274749755859375f, 0.006035462953150272369384765625f, -0.00381429842673242092132568359375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * (-2.0f).xx), 0.0f) * 1.0f).x);
    res += (float4(-0.0511216484010219573974609375f, 0.0695900619029998779296875f, -0.0402262769639492034912109375f, -0.26999187469482421875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.0568219460546970367431640625f, 0.014323373325169086456298828125f, 0.0573288090527057647705078125f, 0.13294033706188201904296875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.074902988970279693603515625f, -0.0856373012065887451171875f, 0.02754732780158519744873046875f, 0.087011717259883880615234375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.005916827358305454254150390625f, 0.0431331694126129150390625f, 0.011086135171353816986083984375f, -0.0060971858911216259002685546875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.0023135771043598651885986328125f, -0.03565101325511932373046875f, -0.0198473632335662841796875f, 0.15186250209808349609375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.0796200335025787353515625f, -0.03324306011199951171875f, 0.16618941724300384521484375f, 0.0414402447640895843505859375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * (-1.0f).xx), 0.0f) * 1.0f).x);
    res += (float4(0.391343593597412109375f, -0.0070745791308581829071044921875f, -0.3234040439128875732421875f, -0.3177863061428070068359375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.2875826358795166015625f, -0.15289388597011566162109375f, -0.0496717989444732666015625f, -0.084753356873989105224609375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.011888044886291027069091796875f, -0.0508498139679431915283203125f, -0.00215163105167448520660400390625f, 0.103234626352787017822265625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.07032288610935211181640625f, -0.16511420905590057373046875f, -0.119941771030426025390625f, -0.02882885001599788665771484375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.125776767730712890625f, -0.10481382906436920166015625f, 0.4004893004894256591796875f, -0.0309179127216339111328125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.791757524013519287109375f, 0.3903119266033172607421875f, 0.312605917453765869140625f, -0.00347884348593652248382568359375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * 0.0f.xx), 0.0f) * 1.0f).x);
    res += (float4(0.20192600786685943603515625f, 0.12871785461902618408203125f, -0.609987795352935791015625f, 0.22168605029582977294921875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.0255547054111957550048828125f, 0.273510873317718505859375f, 0.13986460864543914794921875f, -0.077327989041805267333984375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.032799623906612396240234375f, 0.039329610764980316162109375f, -0.011884413659572601318359375f, -0.078830718994140625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.0523803792893886566162109375f, 0.26357567310333251953125f, -0.084348388016223907470703125f, 0.2639326751232147216796875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.4495396912097930908203125f, -0.581383407115936279296875f, 0.15500764548778533935546875f, -0.101412333548069000244140625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.027533419430255889892578125f, 0.07606099545955657958984375f, -0.072881124913692474365234375f, -0.032372273504734039306640625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * 1.0f.xx), 0.0f) * 1.0f).x);
    res += (float4(-0.03022874332964420318603515625f, -0.06092645227909088134765625f, 0.02013333328068256378173828125f, -0.02185202203691005706787109375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.007870920933783054351806640625f, -0.0724855363368988037109375f, 0.01604898087680339813232421875f, -0.051989533007144927978515625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.009369975887238979339599609375f, -0.02211819775402545928955078125f, 0.001272144843824207782745361328125f, -0.1051536500453948974609375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.111847825348377227783203125f, 0.065017409622669219970703125f, -0.0050289300270378589630126953125f, 0.1248002946376800537109375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.01070285402238368988037109375f, -0.09386523067951202392578125f, 0.01554690487682819366455078125f, 0.0065242056734859943389892578125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.0103917606174945831298828125f, -0.0048142350278794765472412109375f, -0.006381266750395298004150390625f, 0.005858373828232288360595703125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * 2.0f.xx), 0.0f) * 1.0f).x);
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
