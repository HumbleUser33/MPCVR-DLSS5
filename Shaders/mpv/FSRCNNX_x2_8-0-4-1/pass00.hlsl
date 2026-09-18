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
// Pass 0 of FSRCNNX_x2_8-0-4-1.glsl (feature map 1), translated to HLSL by Shaders/mpv/mpv_shaders.py
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
    float4 res = float4(-0.15724922716617584228515625f, -0.012089683674275875091552734375f, 0.006148763932287693023681640625f, -0.28528487682342529296875f);
    res += (float4(-0.0047900392673909664154052734375f, 0.053744710981845855712890625f, -2.4714463506825268268585205078125e-05f, 0.0066653941757977008819580078125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * (-2.0f).xx), 0.0f) * 1.0f).x);
    res += (float4(0.007314468733966350555419921875f, -0.03090040385723114013671875f, -0.01091813854873180389404296875f, -0.00928403250873088836669921875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.059170089662075042724609375f, 0.19749070703983306884765625f, -0.01973575167357921600341796875f, -0.054655484855175018310546875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.001176438294351100921630859375f, -0.02994510717689990997314453125f, 0.02295873127877712249755859375f, 0.00219088862650096416473388671875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.0098101310431957244873046875f, 0.00809954106807708740234375f, -0.00304520200006663799285888671875f, -0.01320355199277400970458984375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.0168330334126949310302734375f, -0.07437114417552947998046875f, -0.02592616342008113861083984375f, 0.02344804815948009490966796875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.02399337850511074066162109375f, 0.189654171466827392578125f, 0.02077563293278217315673828125f, -0.037033237516880035400390625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * (-1.0f).xx), 0.0f) * 1.0f).x);
    res += (float4(0.009479950182139873504638671875f, -0.065251119434833526611328125f, -0.00042927931644953787326812744140625f, -0.072621218860149383544921875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.029728479683399200439453125f, -0.12101866304874420166015625f, -0.02029293216764926910400390625f, -0.0574462898075580596923828125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.0318185277283191680908203125f, 0.084077537059783935546875f, 0.011045130901038646697998046875f, 0.0415569432079792022705078125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.0253141783177852630615234375f, 0.116825617849826812744140625f, 0.115972958505153656005859375f, 0.0963164269924163818359375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.110361583530902862548828125f, -0.0276833958923816680908203125f, -0.499959409236907958984375f, 0.10538671910762786865234375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(1.11004352569580078125f, 0.06467644870281219482421875f, 0.01540057174861431121826171875f, 0.889158666133880615234375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * 0.0f.xx), 0.0f) * 1.0f).x);
    res += (float4(0.122933067381381988525390625f, 0.17194688320159912109375f, 0.573033809661865234375f, -0.164554417133331298828125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.00904427282512187957763671875f, -0.30239617824554443359375f, -0.158949315547943115234375f, 0.0418574027717113494873046875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.00319420360028743743896484375f, -0.131092607975006103515625f, 0.0075543406419456005096435546875f, -0.001644934643991291522979736328125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.099515028297901153564453125f, -0.070192120969295501708984375f, -0.01308958791196346282958984375f, 0.13441701233386993408203125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.0060519003309309482574462890625f, -0.1533465683460235595703125f, 0.01141940057277679443359375f, 0.02646839059889316558837890625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.02440080232918262481689453125f, 0.1881769001483917236328125f, -0.02063511498272418975830078125f, -0.0628309547901153564453125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * 1.0f.xx), 0.0f) * 1.0f).x);
    res += (float4(0.007571312598884105682373046875f, 0.05085943639278411865234375f, 0.04304231703281402587890625f, -0.01241887919604778289794921875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.01668758690357208251953125f, -0.004786551930010318756103515625f, 0.000671912333928048610687255859375f, 0.031680323183536529541015625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.005846126936376094818115234375f, 0.0990798473358154296875f, -0.01777438260614871978759765625f, -0.006612229160964488983154296875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.09724019467830657958984375f, -0.02254463732242584228515625f, -0.00376935745589435100555419921875f, 0.1953062713146209716796875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.0216837190091609954833984375f, -0.1824268400669097900390625f, 0.00698162615299224853515625f, 0.0283037684857845306396484375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.002576799131929874420166015625f, 0.045982711017131805419921875f, -0.008021608926355838775634765625f, 0.00841347873210906982421875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * 2.0f.xx), 0.0f) * 1.0f).x);
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
