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
// Pass 1 of FSRCNNX_x2_8-0-4-1.glsl (feature map 2), translated to HLSL by Shaders/mpv/mpv_shaders.py
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
    float4 res = float4(0.05414475500583648681640625f, 0.008830674923956394195556640625f, -0.011238957755267620086669921875f, -0.012786095030605792999267578125f);
    res += (float4(0.01426600106060504913330078125f, 0.0137931071221828460693359375f, 0.006118810735642910003662109375f, -0.010413422249257564544677734375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * (-2.0f).xx), 0.0f) * 1.0f).x);
    res += (float4(0.014729280956089496612548828125f, -0.0289912857115268707275390625f, 0.02667694352567195892333984375f, 0.0933856964111328125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.17343382537364959716796875f, 0.111631669104099273681640625f, -0.19731573760509490966796875f, -0.0581855811178684234619140625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.034750722348690032958984375f, -0.03415666520595550537109375f, 0.0061667622067034244537353515625f, 0.007525888271629810333251953125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.006988436914980411529541015625f, -0.01942502148449420928955078125f, 0.008083012886345386505126953125f, -0.0036874092184007167816162109375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.02337642014026641845703125f, 0.034474499523639678955078125f, 0.01621459424495697021484375f, 0.097952999174594879150390625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.12807969748973846435546875f, -0.101833917200565338134765625f, -0.01329771988093852996826171875f, -0.001947462209500372409820556640625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * (-1.0f).xx), 0.0f) * 1.0f).x);
    res += (float4(0.4286882579326629638671875f, 0.122267775237560272216796875f, 0.704669415950775146484375f, 0.094547569751739501953125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.110744178295135498046875f, -0.013443307019770145416259765625f, -0.01749009080231189727783203125f, -0.16864454746246337890625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.032147862017154693603515625f, 0.006535784341394901275634765625f, 0.03008059971034526824951171875f, 0.042011328041553497314453125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.124034158885478973388671875f, 0.095030330121517181396484375f, -0.012964845634996891021728515625f, -0.2681856453418731689453125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.484696090221405029296875f, 0.0351924635469913482666015625f, 0.022304333746433258056640625f, -0.12736307084560394287109375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-1.93795073032379150390625f, -0.24444420635700225830078125f, 0.0291962660849094390869140625f, -0.383557856082916259765625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * 0.0f.xx), 0.0f) * 1.0f).x);
    res += (float4(0.63962781429290771484375f, -0.07659383118152618408203125f, -0.0552659817039966583251953125f, 0.439354598522186279296875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.19697280228137969970703125f, -0.0607173256576061248779296875f, 0.01311135478317737579345703125f, 0.054201781749725341796875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.009169600903987884521484375f, -0.0031533432193100452423095703125f, -0.036877758800983428955078125f, -0.0459998287260532379150390625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.109699249267578125f, 0.2597902715206146240234375f, 0.03048696927726268768310546875f, -0.01952007226645946502685546875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.2889648377895355224609375f, -0.427559196949005126953125f, -0.741415679454803466796875f, 0.26954424381256103515625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.0892018377780914306640625f, -0.02291375584900379180908203125f, 0.0244414471089839935302734375f, -0.1926898956298828125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * 1.0f.xx), 0.0f) * 1.0f).x);
    res += (float4(0.05763585865497589111328125f, 0.00278469733893871307373046875f, -0.00368615053594112396240234375f, -0.02535471133887767791748046875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.01596240699291229248046875f, 0.0319602824747562408447265625f, 0.0019470085389912128448486328125f, 0.008978049270808696746826171875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.0552792511880397796630859375f, 0.05430543422698974609375f, 0.01340628229081630706787109375f, 0.0545728243887424468994140625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.117009222507476806640625f, 0.1963327825069427490234375f, 0.150389015674591064453125f, 0.18918283283710479736328125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.00844217836856842041015625f, 0.129701793193817138671875f, -0.03306008875370025634765625f, -0.094206370413303375244140625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.01184404082596302032470703125f, -0.033787585794925689697265625f, 0.005506346933543682098388671875f, 0.0254479162395000457763671875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * 2.0f.xx), 0.0f) * 1.0f).x);
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
