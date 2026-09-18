/*
 * (C) 2026 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <d3d11.h>
#include <atlbase.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// A pass of an mpv user shader, as Shaders/mpv/mpv_shaders.py translated it.
struct MpvPassInfo {
	const char* desc;
	const char* hooks;  // the planes it hooks, space separated
	const char* binds;  // the textures it reads, space separated
	const char* save;   // the name its output is saved under; nullptr: it replaces the plane
	const char* width;  // output size in reverse polish; nullptr: the plane's
	const char* height;
	const char* when;   // condition in reverse polish; nullptr: always
	UINT resid;         // compiled pixel shader
};

// A lookup table of a user shader (//!TEXTURE), in RGBA16F texels.
struct MpvTextureInfo {
	const char* name;
	UINT width;
	UINT height;
	bool linear;
	UINT resid;
};

struct MpvShaderInfo {
	const wchar_t* name;
	const MpvPassInfo* passes;
	UINT passCount;
	const MpvTextureInfo* textures;
	UINT textureCount;
};

// Runs a prescaler translated from an mpv user shader (FSRCNNX, RAVU) on one plane,
// the way libplacebo does: each pass renders into an RGBA16F texture of the size its
// WIDTH and HEIGHT give, reading the plane, what earlier passes saved and the
// shader's lookup tables. What the last pass that replaces the plane writes is the
// enlarged plane. Needs feature level 11.0: the passes are shader model 5.
class CMpvShader
{
public:
	struct Texture {
		CComPtr<ID3D11Texture2D> pTexture;
		CComPtr<ID3D11ShaderResourceView> pShaderResource;
		CComPtr<ID3D11RenderTargetView> pRenderTarget;
		UINT width = 0;
		UINT height = 0;

		HRESULT CheckCreate(ID3D11Device* pDevice, UINT w, UINT h);
		void Release();
	};

	// Hands out the bytes of a compiled shader or a table: the renderer's resources,
	// or files for the harness. They only have to live while Load runs.
	using DataSource = std::function<bool(UINT resid, const BYTE*& data, size_t& size)>;

	HRESULT Load(ID3D11Device* pDevice, const MpvShaderInfo& info, UINT vertexShaderResid, const DataSource& source);
	void Release();
	bool IsLoaded() const { return m_pInfo != nullptr; }
	const MpvShaderInfo* Info() const { return m_pInfo; }

	// How many textures the passes are holding, for the record: FSRCNNX 16 has
	// twenty-six passes but never needs that many at once.
	UINT TextureCount() const { return (UINT)m_pool.size(); }

	// Whether the passes run for a plane of inW x inH shown at outW x outH, and the
	// size of the plane they leave.
	bool Applies(UINT inW, UINT inH, UINT outW, UINT outH, UINT* pW = nullptr, UINT* pH = nullptr) const;

	// Enlarges the plane pInput holds in red (inW x inH), for a picture shown at
	// outW x outH, into out, made the size the passes give. shiftX and shiftY move
	// where the plane is read, in its normalized coordinates: chroma that does not
	// sit at the centre of its luma pixels. S_FALSE when the passes do not apply.
	HRESULT Process(ID3D11DeviceContext* pContext, ID3D11ShaderResourceView* pInput, UINT inW, UINT inH,
		UINT outW, UINT outH, float shiftX, float shiftY, Texture& out);

private:
	struct Bind {
		enum Kind { Plane, Saved, Table } kind = Plane;
		int index = 0; // Saved: slot of the name, Table: index of the table
	};
	struct Pass {
		const MpvPassInfo* pInfo = nullptr;
		std::vector<std::string> hooks;
		std::vector<Bind> binds;
		int saveSlot = -1; // -1: replaces the plane
		std::vector<std::string> when, width, height;
		CComPtr<ID3D11PixelShader> pShader;
	};
	struct Table {
		Texture texture;
		bool linear = true;
	};
	struct Plan {
		std::vector<bool> run;
		std::vector<UINT> w, h;
		std::vector<int> lastReader; // the last pass that reads a pass's output, -1: none
		UINT planeW = 0, planeH = 0;
		int lastPlanePass = -1;
	};

	bool MakePlan(UINT inW, UINT inH, UINT outW, UINT outH, Plan& plan) const;

	// FSRCNNX keeps up to twelve feature maps alive at once but has twenty-six passes:
	// a texture goes back to the pool as soon as the last pass that reads it has run,
	// which is what libplacebo does too.
	Texture* Acquire(UINT w, UINT h);
	void ReleaseToPool(const Texture* pTexture);

	struct Pooled {
		Texture texture;
		bool bInUse = false;
	};
	std::vector<std::unique_ptr<Pooled>> m_pool; // stable addresses: passes hold pointers
	std::vector<Texture*> m_passOutput;          // what each pass wrote, while it is alive
	UINT m_poolFor[4] = {};                      // the sizes the pool was filled for

	const MpvShaderInfo* m_pInfo = nullptr;
	std::vector<Pass> m_passes;
	std::vector<Table> m_tables;
	std::vector<std::string> m_saveNames;
	std::vector<const Texture*> m_saved; // what each saved name holds while the passes run
	CComPtr<ID3D11Device> m_pDevice;
	CComPtr<ID3D11VertexShader> m_pVertexShader;
	CComPtr<ID3D11Buffer> m_pConstants;
	CComPtr<ID3D11SamplerState> m_pSamplerLinear;
	CComPtr<ID3D11SamplerState> m_pSamplerPoint;
	Plan m_plan; // kept between pictures to spare the allocations
};
