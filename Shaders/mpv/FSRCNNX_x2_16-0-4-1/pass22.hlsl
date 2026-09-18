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
// Pass 22 of FSRCNNX_x2_16-0-4-1.glsl (sub-band residuals 3), translated to HLSL by Shaders/mpv/mpv_shaders.py
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
Texture2D<float4> FEATURE3_raw : register(t5);
SamplerState _FEATURE3_raw_sampler : register(s5);

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
    float4 res = float4(-0.0231755860149860382080078125f, -0.01230032742023468017578125f, -0.04318772256374359130859375f, -0.048206232488155364990234375f);
    res += mul(MODEL1_raw.SampleLevel(_MODEL1_raw_sampler, (pl_pos + _57_pl_tex_shift[0].xy) + (_57_pl_tex_size[0].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(0.006168833933770656585693359375f, -0.3225260674953460693359375f, -0.1020668447017669677734375f, 0.07565595209598541259765625f), float4(0.01903223432600498199462890625f, 0.0429529212415218353271484375f, 0.013356645591557025909423828125f, -0.0357269980013370513916015625f), float4(-0.05119757354259490966796875f, 0.0205178000032901763916015625f, 0.080269582569599151611328125f, -0.06997247040271759033203125f), float4(0.072755239903926849365234375f, -0.0051232990808784961700439453125f, -0.023181177675724029541015625f, 0.0385252349078655242919921875f)));
    res += mul(MODEL2_raw.SampleLevel(_MODEL2_raw_sampler, (pl_pos + _57_pl_tex_shift[1].xy) + (_57_pl_tex_size[1].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(0.00037962736678309738636016845703125f, -0.15141569077968597412109375f, -0.076611019670963287353515625f, 0.02786938659846782684326171875f), float4(0.3816023766994476318359375f, 0.18429534137248992919921875f, -0.342725098133087158203125f, -0.0513318441808223724365234375f), float4(0.078396372497081756591796875f, 0.103416956961154937744140625f, 0.14139603078365325927734375f, 0.1308334171772003173828125f), float4(0.006715382449328899383544921875f, 0.01581440679728984832763671875f, -0.0463985614478588104248046875f, 0.12294714152812957763671875f)));
    res += mul(MODEL3_raw.SampleLevel(_MODEL3_raw_sampler, (pl_pos + _57_pl_tex_shift[2].xy) + (_57_pl_tex_size[2].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(0.13856680691242218017578125f, -0.333245456218719482421875f, -0.0325091183185577392578125f, -0.0500471331179141998291015625f), float4(0.01092767901718616485595703125f, 0.13464109599590301513671875f, -0.21942384541034698486328125f, -0.384137213230133056640625f), float4(0.203024387359619140625f, 0.0367027409374713897705078125f, 0.074623584747314453125f, 0.08589453995227813720703125f), float4(-0.0915820300579071044921875f, 0.4482104778289794921875f, -0.37054812908172607421875f, -0.463193237781524658203125f)));
    res += mul(MODEL4_raw.SampleLevel(_MODEL4_raw_sampler, (pl_pos + _57_pl_tex_shift[3].xy) + (_57_pl_tex_size[3].zw * 0.0f.xx), 0.0f) * 1.0f, float4x4(float4(0.077257789671421051025390625f, -0.4344681203365325927734375f, -0.5865671634674072265625f, -0.9034788608551025390625f), float4(0.0337008647620677947998046875f, -0.011914782226085662841796875f, 0.00287201069295406341552734375f, 0.0309769921004772186279296875f), float4(-0.091188848018646240234375f, 0.2908170223236083984375f, 0.02355944924056529998779296875f, 0.009843817912042140960693359375f), float4(0.01860149204730987548828125f, -0.056825838983058929443359375f, -0.0455871261656284332275390625f, -0.106958307325839996337890625f)));
    res += (FEATURE3_raw.SampleLevel(_FEATURE3_raw_sampler, (pl_pos + _57_pl_tex_shift[4].xy) + (_57_pl_tex_size[4].zw * 0.0f.xx), 0.0f) * 1.0f);
    res = max(res, 0.0f.xxxx) + (float4(0.098469294607639312744140625f, -0.05284337699413299560546875f, 0.1861927509307861328125f, -0.079391337931156158447265625f) * min(res, 0.0f.xxxx));
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
