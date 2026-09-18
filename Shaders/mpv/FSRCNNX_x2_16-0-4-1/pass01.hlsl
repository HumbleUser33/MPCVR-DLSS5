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
// Pass 1 of FSRCNNX_x2_16-0-4-1.glsl (feature map 2), translated to HLSL by Shaders/mpv/mpv_shaders.py
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
    float4 res = float4(0.003512640483677387237548828125f, -0.1424804031848907470703125f, 0.009801924228668212890625f, -0.23243476450443267822265625f);
    res += (float4(-0.00815995596349239349365234375f, 0.01534012146294116973876953125f, -8.8481981947552412748336791992188e-05f, -0.0041208495385944843292236328125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * (-2.0f).xx), 0.0f) * 1.0f).x);
    res += (float4(0.0348629020154476165771484375f, -0.02610845305025577545166015625f, 0.14123819768428802490234375f, 0.012064841575920581817626953125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.01822018064558506011962890625f, 0.0420538075268268585205078125f, 0.12791036069393157958984375f, 0.0005762653308920562267303466796875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.007424036972224712371826171875f, 0.0205862484872341156005859375f, -0.02487925626337528228759765625f, -0.009688339196145534515380859375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.014757598750293254852294921875f, -0.013880114071071147918701171875f, 0.0336018688976764678955078125f, 0.010141917504370212554931640625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-2.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.016830019652843475341796875f, -0.0323963575065135955810546875f, -0.110362827777862548828125f, 0.0184982456266880035400390625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.033625803887844085693359375f, 0.00904605351388454437255859375f, 0.1413228511810302734375f, 0.02139510214328765869140625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * (-1.0f).xx), 0.0f) * 1.0f).x);
    res += (float4(-0.21929858624935150146484375f, -0.121087558567523956298828125f, -0.59162580966949462890625f, 0.14279098808765411376953125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.0442710407078266143798828125f, -0.0331254564225673675537109375f, 0.24504663050174713134765625f, 0.0069104693830013275146484375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.038544900715351104736328125f, -0.01560254581272602081298828125f, 0.013066531158983707427978515625f, 0.0061669168062508106231689453125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(-1.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.016250722110271453857421875f, 0.101315729320049285888671875f, 0.1922776401042938232421875f, -0.02959075011312961578369140625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.41034615039825439453125f, -0.26874828338623046875f, 0.28031957149505615234375f, 0.23680321872234344482421875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.059454299509525299072265625f, 1.11061084270477294921875f, -0.0012417822144925594329833984375f, 0.6573531627655029296875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * 0.0f.xx), 0.0f) * 1.0f).x);
    res += (float4(-0.32314503192901611328125f, -0.08182166516780853271484375f, -0.078077264130115509033203125f, 0.0890857279300689697265625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.0311414338648319244384765625f, 0.011479658074676990509033203125f, -0.033379755914211273193359375f, 0.02200090698897838592529296875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(0.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.000567852635867893695831298828125f, -0.010871519334614276885986328125f, -0.14756558835506439208984375f, 0.012469197623431682586669921875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.08596648275852203369140625f, -0.00963619910180568695068359375f, -0.282512009143829345703125f, 0.038032911717891693115234375f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.02968977950513362884521484375f, -0.330385386943817138671875f, 0.0030247638933360576629638671875f, 0.192808926105499267578125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.106448478996753692626953125f, -0.08958099782466888427734375f, 0.19888101518154144287109375f, 0.07612590491771697998046875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * 1.0f.xx), 0.0f) * 1.0f).x);
    res += (float4(-0.011481807567179203033447265625f, 0.01295008324086666107177734375f, -0.084782682359218597412109375f, -0.01884375698864459991455078125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(1.0f, 2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.0039359503425657749176025390625f, -0.0073066619224846363067626953125f, 0.031617276370525360107421875f, 0.0056592230685055255889892578125f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, -2.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.014325539581477642059326171875f, -0.00378818926401436328887939453125f, -0.1154748499393463134765625f, -0.009480874054133892059326171875f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, -1.0f)), 0.0f) * 1.0f).x);
    res += (float4(0.0334111787378787994384765625f, 0.0511705093085765838623046875f, 0.10905611515045166015625f, 0.0004155720234848558902740478515625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, 0.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.01737861894071102142333984375f, -0.01892531104385852813720703125f, -0.09711380302906036376953125f, -0.0048367069102823734283447265625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * float2(2.0f, 1.0f)), 0.0f) * 1.0f).x);
    res += (float4(-0.00205643265508115291595458984375f, -0.008842992596328258514404296875f, 0.0409048907458782196044921875f, 0.009855412878096103668212890625f) * (LUMA_raw.SampleLevel(_LUMA_raw_sampler, (pl_pos + _40_pl_tex_shift[0].xy) + (_40_pl_tex_size[0].zw * 2.0f.xx), 0.0f) * 1.0f).x);
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
