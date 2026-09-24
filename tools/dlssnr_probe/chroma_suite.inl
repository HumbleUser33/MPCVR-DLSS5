// --tchroma: what each chroma upsampler rebuilds from 4:2:0.
//
// Included by harness.cpp after upscale_suite.inl, whose references, metrics
// helpers and zoom boards it uses. Each reference -- and its 2x reduction, for
// video at 1080p -- is turned into Y'CbCr (BT.709, full range), its chroma
// averaged over 2x2 blocks into a 4:2:0 picture with centred chroma, then the
// chroma is brought back to full size by each method and the picture turned
// back into RGB. The renderer's Bilinear and Catmull-Rom are reimplemented here
// on the same centred positions, next to the mpv chroma shaders translated by
// mpv_shaders.py (KrigBilateral, CfL), which see the full-size luma as well, and
// to mpv's luma doublers (FSRCNNX, RAVU) run on Cb and on Cr as if each were luma.

namespace temporal {

namespace {

constexpr float kCb = 1.8556f, kCr = 1.5748f;   // BT.709: B - Y and R - Y over these

// jinc(x) = 2 J1(pi x) / (pi x): the polar kernel "Jinc" means in madVR and in
// mpv's ewa_lanczos, windowed by itself at the radius, as libplacebo does it.
double Jinc(double x)
{
	if (x < 1e-8) {
		return 1.0;
	}
	const double t = 3.14159265358979323846 * x;
	return 2.0 * std::cyl_bessel_j(1.0, t) / t;
}

// Chroma at (x, y) of the full-size picture, read from the half-size plane at
// its centred position with a polar kernel of radius 3.2383 -- the third zero of
// jinc, mpv's default for ewa_lanczos.
float SampleChromaJinc(const std::vector<float>& plane, int cw, int ch, int x, int y)
{
	constexpr double kRadius = 3.2383;      // third zero of jinc, mpv's ewa_lanczos
	constexpr double kFirstZero = 1.2196699;
	const double u = (x + 0.5) / 2 - 0.5, v = (y + 0.5) / 2 - 0.5;
	const int ix = (int)std::floor(u), iy = (int)std::floor(v);
	double acc = 0, weight = 0;
	const int reach = (int)std::ceil(kRadius);
	for (int j = -reach; j <= reach + 1; j++) {
		for (int i = -reach; i <= reach + 1; i++) {
			const double dx = ix + i - u, dy = iy + j - v;
			const double d = std::sqrt(dx * dx + dy * dy);
			if (d >= kRadius) {
				continue;
			}
			const double w = Jinc(d) * Jinc(d * kFirstZero / kRadius);
			acc += w * plane[(size_t)std::clamp(iy + j, 0, ch - 1) * cw + std::clamp(ix + i, 0, cw - 1)];
			weight += w;
		}
	}
	return weight != 0 ? (float)(acc / weight) : 0.0f;
}

// Anti-ringing, the way libplacebo does it: a pixel is pulled back towards the
// range the samples around it really cover, by `strength` of the way. 0.8 is
// where libplacebo settles, and the doom9 test shows what it buys.
float AntiRing(float value, const std::vector<float>& plane, int cw, int ch, int x, int y, float strength)
{
	const double u = (x + 0.5) / 2 - 0.5, v = (y + 0.5) / 2 - 0.5;
	const int ix = (int)std::floor(u), iy = (int)std::floor(v);
	float lo = 1e9f, hi = -1e9f;
	for (int j = 0; j <= 1; j++) {
		for (int i = 0; i <= 1; i++) {
			const float sample = plane[(size_t)std::clamp(iy + j, 0, ch - 1) * cw + std::clamp(ix + i, 0, cw - 1)];
			lo = std::min(lo, sample);
			hi = std::max(hi, sample);
		}
	}
	return value + strength * (std::clamp(value, lo, hi) - value);
}

// Chroma at (x, y) of the full-size picture, read from the half-size plane at
// its centred position with a separable kernel of 2 (bilinear) or 4 taps.
float SampleChroma(const std::vector<float>& plane, int cw, int ch, int x, int y, bool cubic)
{
	const float u = (x + 0.5f) / 2 - 0.5f, v = (y + 0.5f) / 2 - 0.5f;
	const int ix = (int)std::floor(u), iy = (int)std::floor(v);
	const float fx = u - ix, fy = v - iy;
	auto at = [&](int px, int py) {
		return plane[(size_t)std::clamp(py, 0, ch - 1) * cw + std::clamp(px, 0, cw - 1)];
	};
	if (!cubic) {
		return (at(ix, iy) * (1 - fx) + at(ix + 1, iy) * fx) * (1 - fy)
		     + (at(ix, iy + 1) * (1 - fx) + at(ix + 1, iy + 1) * fx) * fy;
	}
	// Catmull-Rom, the renderer's B=0 C=0.5 bicubic.
	auto weights = [](float t, float w[4]) {
		const float t2 = t * t, t3 = t2 * t;
		w[0] = -0.5f * t3 + t2 - 0.5f * t;
		w[1] = 1.5f * t3 - 2.5f * t2 + 1.0f;
		w[2] = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
		w[3] = 0.5f * t3 - 0.5f * t2;
	};
	float wx[4], wy[4];
	weights(fx, wx);
	weights(fy, wy);
	float acc = 0;
	for (int j = 0; j < 4; j++) {
		float row = 0;
		for (int i = 0; i < 4; i++) {
			row += wx[i] * at(ix - 1 + i, iy - 1 + j);
		}
		acc += wy[j] * row;
	}
	return acc;
}

// A plane moved by part of a pixel with Catmull-Rom, for a prescaler that says it
// writes its result off the grid (NNEDI3).
void ShiftChroma(std::vector<float>& plane, int W, int H, double dx, double dy)
{
	if (dx == 0 && dy == 0) {
		return;
	}
	auto weights = [](double t, double w[4]) {
		const double t2 = t * t, t3 = t2 * t;
		w[0] = -0.5 * t3 + t2 - 0.5 * t;
		w[1] = 1.5 * t3 - 2.5 * t2 + 1.0;
		w[2] = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
		w[3] = 0.5 * t3 - 0.5 * t2;
	};
	const std::vector<float> src(plane);
	double wx[4], wy[4];
	weights(dx - std::floor(dx), wx);
	weights(dy - std::floor(dy), wy);
	const int ox = (int)std::floor(dx), oy = (int)std::floor(dy);
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			double acc = 0;
			for (int j = 0; j < 4; j++) {
				double row = 0;
				for (int i = 0; i < 4; i++) {
					row += wx[i] * src[(size_t)std::clamp(y + oy - 1 + j, 0, H - 1) * W + std::clamp(x + ox - 1 + i, 0, W - 1)];
				}
				acc += wy[j] * row;
			}
			plane[(size_t)y * W + x] = (float)acc;
		}
	}
}

struct ChromaMetrics {
	double psnrChroma = 0;   // Cb and Cr together
	double psnrRgb = 0;
	double psnrEdges = 0;    // chroma, where the luma has edges: bleeding shows there
	double ssimChroma = 0;
};

double PlaneSsim(const std::vector<float>& a, const std::vector<float>& b, int W, int H)
{
	const double C1 = 0.01 * 0.01, C2 = 0.03 * 0.03;
	double sum = 0;
	long long count = 0;
	for (int y0 = 0; y0 + 8 <= H; y0 += 4) {
		for (int x0 = 0; x0 + 8 <= W; x0 += 4) {
			double ma = 0, mb = 0;
			for (int y = 0; y < 8; y++) {
				for (int x = 0; x < 8; x++) {
					ma += a[(size_t)(y0 + y) * W + x0 + x];
					mb += b[(size_t)(y0 + y) * W + x0 + x];
				}
			}
			ma /= 64; mb /= 64;
			double va = 0, vb = 0, cov = 0;
			for (int y = 0; y < 8; y++) {
				for (int x = 0; x < 8; x++) {
					const double da = a[(size_t)(y0 + y) * W + x0 + x] - ma, db = b[(size_t)(y0 + y) * W + x0 + x] - mb;
					va += da * da; vb += db * db; cov += da * db;
				}
			}
			va /= 63; vb /= 63; cov /= 63;
			sum += ((2 * ma * mb + C1) * (2 * cov + C2)) / ((ma * ma + mb * mb + C1) * (va + vb + C2));
			count++;
		}
	}
	return count ? sum / count : 0;
}

ChromaMetrics MeasureChroma(const std::vector<float>& cb, const std::vector<float>& cr,
                            const std::vector<float>& refCb, const std::vector<float>& refCr,
                            const std::vector<float>& y, const std::vector<float>& refRgba, int W, int H,
                            std::vector<float>* rgbaOut)
{
	ChromaMetrics m;
	double seChroma = 0, seRgb = 0, seEdges = 0;
	long long edges = 0;
	if (rgbaOut) {
		rgbaOut->assign(4 * (size_t)W * H, 1.0f);
	}
	for (int j = 0; j < H; j++) {
		for (int i = 0; i < W; i++) {
			const size_t k = (size_t)j * W + i;
			const double dcb = cb[k] - refCb[k], dcr = cr[k] - refCr[k];
			seChroma += dcb * dcb + dcr * dcr;
			const float r = y[k] + kCr * (cr[k] - 0.5f);
			const float b = y[k] + kCb * (cb[k] - 0.5f);
			const float g = (y[k] - 0.2126f * r - 0.0722f * b) / 0.7152f;
			const float rgb[3] = { r, g, b };
			for (int c = 0; c < 3; c++) {
				const double d = std::clamp(rgb[c], 0.0f, 1.0f) - refRgba[4 * k + c];
				seRgb += d * d;
				if (rgbaOut) {
					(*rgbaOut)[4 * k + c] = std::clamp(rgb[c], 0.0f, 1.0f);
				}
			}
			if (i > 0 && j > 0 && i < W - 1 && j < H - 1) {
				const float g2 = std::abs(y[k + 1] - y[k - 1]) + std::abs(y[k + W] - y[k - W]);
				if (g2 > 0.08f) {
					seEdges += dcb * dcb + dcr * dcr;
					edges++;
				}
			}
		}
	}
	const double n = (double)W * H;
	auto psnr = [](double mse) { return mse > 0 ? 10.0 * std::log10(1.0 / mse) : 99.0; };
	m.psnrChroma = psnr(seChroma / (2 * n));
	m.psnrRgb = psnr(seRgb / (3 * n));
	m.psnrEdges = edges ? psnr(seEdges / (2.0 * edges)) : 99.0;
	m.ssimChroma = 0.5 * (PlaneSsim(cb, refCb, W, H) + PlaneSsim(cr, refCr, W, H));
	return m;
}

} // namespace

static int RunChroma(ID3D11Device* dev, ID3D11DeviceContext* ctx, int maxRefs)
{
	Head("Chroma suite: what each upsampler rebuilds from 4:2:0");

	const HRESULT coInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	int rc = 0;
	{
		CComPtr<IWICImagingFactory> factory;
		Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))), "WIC factory");

		std::vector<std::wstring> files;
		{
			WIN32_FIND_DATAW fd = {};
			for (const wchar_t* pattern : { L"upscale_refs\\*.png", L"upscale_refs\\*.jpg" }) {
				HANDLE h = FindFirstFileW(pattern, &fd);
				if (h != INVALID_HANDLE_VALUE) {
					do {
						files.push_back(std::wstring(L"upscale_refs\\") + fd.cFileName);
					} while (FindNextFileW(h, &fd));
					FindClose(h);
				}
			}
			std::sort(files.begin(), files.end());
			if (maxRefs > 0 && files.size() > (size_t)maxRefs) {
				files.resize((size_t)maxRefs);
			}
		}
		if (files.empty()) {
			printf("  no picture in upscale_refs\n");
			rc = 1;
		}

		std::vector<std::unique_ptr<CMpvShader>> shaders;
		if (!rc) {
			for (const wchar_t* name : { L"KrigBilateral", L"CfL_Prediction", L"CfL_Prediction_Lite" }) {
				auto shader = std::make_unique<CMpvShader>();
				std::string error;
				if (!shader->Load(dev, std::wstring(L"upscalers\\hlsl\\") + name, error)) {
					printf("  %S: %s\n", name, error.c_str());
					continue;
				}
				printf("  mpv shader %s, %d passes\n", shader->Name().c_str(), shader->PassCount());
				shaders.push_back(std::move(shader));
			}
		}
		std::vector<std::unique_ptr<CMpvShader>> doublers;
		if (!rc) {
			for (const wchar_t* name : { L"FSRCNNX_x2_8-0-4-1", L"FSRCNNX_x2_16-0-4-1", L"ravu-zoom-ar-r3",
					L"ravu-lite-ar-r4", L"ArtCNN_C4F16", L"ArtCNN_C4F16_DS", L"nnedi3-nns32-win8x4" }) {
				auto shader = std::make_unique<CMpvShader>();
				std::string error;
				if (!shader->Load(dev, std::wstring(L"upscalers\\hlsl\\") + name, error)) {
					printf("  %S: %s\n", name, error.c_str());
					continue;
				}
				printf("  mpv luma shader %s on each chroma plane, %d passes\n", shader->Name().c_str(), shader->PassCount());
				doublers.push_back(std::move(shader));
			}
		}

		CComPtr<ID3D11Query> disjoint, tsStart, tsEnd;
		if (!rc) {
			D3D11_QUERY_DESC qd = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
			dev->CreateQuery(&qd, &disjoint);
			qd.Query = D3D11_QUERY_TIMESTAMP;
			dev->CreateQuery(&qd, &tsStart);
			dev->CreateQuery(&qd, &tsEnd);
			if (!disjoint || !tsStart || !tsEnd) {
				rc = 1;
			}
		}

		if (!rc) {
			g_report = fopen("temporal_results_chroma.txt", "w");
			Out("\nChroma upsampling from 4:2:0. Each reference is turned into Y'CbCr (BT.709, full range), its chroma\n");
			Out("averaged over 2x2 blocks with centred siting, brought back to full size by each method, and the picture\n");
			Out("turned back into RGB.\n\n");
			Out("psnr-c: Cb and Cr against the reference's; psnr-e: the same where the luma has edges, where bleeding\n");
			Out("shows; ssim-c: chroma SSIM; psnr-rgb: the whole picture. ms: GPU time of the shader passes.\n");
			Out("The luma doublers (FSRCNNX, RAVU) see Cb, then Cr, each alone as the plane they hook; their time\n");
			Out("covers both planes.\n");

			for (const std::wstring& file : files) {
				UpscaleRef ref;
				std::string error;
				ref.name = file.substr(file.find_last_of(L'\\') + 1);
				if (!LoadReference(factory, file.c_str(), ref, error)) {
					Out("\n%S: %s\n", ref.name.c_str(), error.c_str());
					g_failures++;
					continue;
				}

				for (int scale = 1; scale <= 3; scale++) {
					// The reference itself, then its 2x reduction: chroma of 4K and of 1080p video.
					// Case 3 is a control at 1080p: chroma an exact linear function of luma, which
					// a predictor from luma must rebuild almost perfectly once aligned right.
					const bool control = (scale == 3);
					std::vector<float> rgba;
					int W = ref.W, H = ref.H;
					if (scale >= 2) {
						DownscaleBox(ref.rgba, ref.W, ref.H, 2, rgba);
						W /= 2;
						H /= 2;
					} else {
						rgba = ref.rgba;
					}
					W &= ~1;
					H &= ~1;
					const int cw = W / 2, chh = H / 2;

					std::vector<float> Y((size_t)W * H), refCb((size_t)W * H), refCr((size_t)W * H);
					std::vector<float> rgbaTrim(4 * (size_t)W * H);
					const int srcW = (scale >= 2) ? ref.W / 2 : ref.W;
					for (int j = 0; j < H; j++) {
						for (int i = 0; i < W; i++) {
							const float* p = &rgba[4 * ((size_t)j * srcW + i)];
							const size_t k = (size_t)j * W + i;
							Y[k] = Luma(p);
							if (control) {
								refCb[k] = 0.5f + 0.3f * (Y[k] - 0.5f);
								refCr[k] = 0.5f - 0.2f * (Y[k] - 0.5f);
								const float r = Y[k] + kCr * (refCr[k] - 0.5f), b = Y[k] + kCb * (refCb[k] - 0.5f);
								const float g = (Y[k] - 0.2126f * r - 0.0722f * b) / 0.7152f;
								rgbaTrim[4 * k] = r; rgbaTrim[4 * k + 1] = g; rgbaTrim[4 * k + 2] = b; rgbaTrim[4 * k + 3] = 1.0f;
							} else {
								std::copy_n(p, 4, &rgbaTrim[4 * k]);
								refCb[k] = (p[2] - Y[k]) / kCb + 0.5f;
								refCr[k] = (p[0] - Y[k]) / kCr + 0.5f;
							}
						}
					}
					std::vector<float> subCb((size_t)cw * chh), subCr((size_t)cw * chh);
					for (int j = 0; j < chh; j++) {
						for (int i = 0; i < cw; i++) {
							const size_t a = (size_t)(2 * j) * W + 2 * i;
							subCb[(size_t)j * cw + i] = 0.25f * (refCb[a] + refCb[a + 1] + refCb[a + W] + refCb[a + W + 1]);
							subCr[(size_t)j * cw + i] = 0.25f * (refCr[a] + refCr[a + 1] + refCr[a + W] + refCr[a + W + 1]);
						}
					}

					Out("\n%S -- %dx%d, chroma %dx%d%s\n", ref.name.c_str(), W, H, cw, chh,
					    control ? ", control: chroma a linear function of luma" : "");
					Out("  %-24s %8s %8s %8s %9s %9s\n", "method", "psnr-c", "psnr-e", "ssim-c", "psnr-rgb", "time");

					std::map<std::string, std::vector<float>> kept;
					auto row = [&](const char* name, const std::vector<float>& cb, const std::vector<float>& cr, double ms, bool gpu) {
						std::vector<float> out;
						const ChromaMetrics m = MeasureChroma(cb, cr, refCb, refCr, Y, rgbaTrim, W, H, &out);
						if (gpu) {
							Out("  %-24s %8.3f %8.3f %8.4f %9.3f %6.2f ms\n", name, m.psnrChroma, m.psnrEdges, m.ssimChroma, m.psnrRgb, ms);
						} else {
							Out("  %-24s %8.3f %8.3f %8.4f %9.3f %9s\n", name, m.psnrChroma, m.psnrEdges, m.ssimChroma, m.psnrRgb, "cpu");
						}
						kept[name] = out;
					};

					// Through half floats on the way in and out, as the shaders see their planes
					// and write their results: the CPU filters get no finer precision than they do.
					auto half = [](float v) {
						return DirectX::PackedVector::XMConvertHalfToFloat(DirectX::PackedVector::XMConvertFloatToHalf(v));
					};
					std::vector<float> subCbHalf(subCb.size()), subCrHalf(subCr.size());
					std::transform(subCb.begin(), subCb.end(), subCbHalf.begin(), half);
					std::transform(subCr.begin(), subCr.end(), subCrHalf.begin(), half);
					// The kernels, each also with anti-ringing where it can overshoot:
					// bilinear cannot, so it is left alone.
					struct Kernel {
						const char* name;
						int kind;          // 0 bilinear, 1 Catmull-Rom, 2 Jinc
						float antiring;    // 0: none
					};
					static const Kernel kKernels[] = {
						{ "Bilinear",       0, 0.0f },
						{ "Catmull-Rom",    1, 0.0f },
						{ "Catmull-Rom AR", 1, 0.8f },
						{ "Jinc (ewa r3)",  2, 0.0f },
						{ "Jinc AR",        2, 0.8f },
					};
					for (const Kernel& kernel : kKernels) {
						std::vector<float> cb((size_t)W * H), cr((size_t)W * H);
						for (int j = 0; j < H; j++) {
							for (int i = 0; i < W; i++) {
								float vb = kernel.kind == 2 ? SampleChromaJinc(subCbHalf, cw, chh, i, j)
									: SampleChroma(subCbHalf, cw, chh, i, j, kernel.kind == 1);
								float vr = kernel.kind == 2 ? SampleChromaJinc(subCrHalf, cw, chh, i, j)
									: SampleChroma(subCrHalf, cw, chh, i, j, kernel.kind == 1);
								if (kernel.antiring > 0) {
									vb = AntiRing(vb, subCbHalf, cw, chh, i, j, kernel.antiring);
									vr = AntiRing(vr, subCrHalf, cw, chh, i, j, kernel.antiring);
								}
								cb[(size_t)j * W + i] = half(vb);
								cr[(size_t)j * W + i] = half(vr);
							}
						}
						row(kernel.name, cb, cr, 0, false);
					}

					// The planes for the shaders: luma alone, and Cb, Cr in red and green.
					std::vector<HALF> yHalf(4 * (size_t)W * H), cHalf(4 * (size_t)cw * chh);
					const HALF zero = DirectX::PackedVector::XMConvertFloatToHalf(0.0f);
					const HALF one = DirectX::PackedVector::XMConvertFloatToHalf(1.0f);
					for (size_t k = 0; k < (size_t)W * H; k++) {
						yHalf[4 * k] = DirectX::PackedVector::XMConvertFloatToHalf(Y[k]);
						yHalf[4 * k + 1] = yHalf[4 * k + 2] = zero;
						yHalf[4 * k + 3] = one;
					}
					for (size_t k = 0; k < (size_t)cw * chh; k++) {
						cHalf[4 * k] = DirectX::PackedVector::XMConvertFloatToHalf(subCb[k]);
						cHalf[4 * k + 1] = DirectX::PackedVector::XMConvertFloatToHalf(subCr[k]);
						cHalf[4 * k + 2] = zero;
						cHalf[4 * k + 3] = one;
					}
					MpvTexture lumaPlane, chromaPlane;
					if (!MakeMpvTexture(dev, W, H, lumaPlane, yHalf.data(), 4 * sizeof(HALF) * W)
							|| !MakeMpvTexture(dev, cw, chh, chromaPlane, cHalf.data(), 4 * sizeof(HALF) * cw)) {
						Out("  plane textures failed\n");
						g_failures++;
						continue;
					}

					// Two warm-up runs, then the average GPU time of four.
					auto timeRuns = [&](auto&& runOnce, double& ms) {
						ms = 0;
						if (!runOnce() || !runOnce()) {
							return false;
						}
						const int kRuns = 4;
						bool ok = true;
						ctx->Begin(disjoint);
						ctx->End(tsStart);
						for (int run = 0; run < kRuns && ok; run++) {
							ok = runOnce();
						}
						ctx->End(tsEnd);
						ctx->End(disjoint);
						D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
						UINT64 t0 = 0, t1 = 0;
						while (ctx->GetData(disjoint, &dj, sizeof(dj), 0) == S_FALSE) {
							Sleep(0);
						}
						while (ctx->GetData(tsStart, &t0, sizeof(t0), 0) == S_FALSE) {
							Sleep(0);
						}
						while (ctx->GetData(tsEnd, &t1, sizeof(t1), 0) == S_FALSE) {
							Sleep(0);
						}
						ms = (!dj.Disjoint && dj.Frequency) ? double(t1 - t0) * 1000.0 / dj.Frequency / kRuns : 0.0;
						return ok;
					};
					// The red and green channels of a full-size result, back on the CPU.
					auto readBack = [&](ID3D11Texture2D* tex, std::vector<float>* red, std::vector<float>* green) {
						D3D11_TEXTURE2D_DESC sd = {};
						tex->GetDesc(&sd);
						sd.Usage = D3D11_USAGE_STAGING;
						sd.BindFlags = 0;
						sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
						sd.MiscFlags = 0;
						CComPtr<ID3D11Texture2D> stage;
						std::vector<float> planeData;
						if (FAILED(dev->CreateTexture2D(&sd, nullptr, &stage))) {
							return false;
						}
						ctx->CopyResource(stage, tex);
						if (!ReadRgbaHalf(ctx, stage, W, H, planeData)) {
							return false;
						}
						for (int c = 0; c < 2; c++) {
							std::vector<float>* out = c ? green : red;
							if (out) {
								out->resize((size_t)W * H);
								for (size_t k = 0; k < (size_t)W * H; k++) {
									(*out)[k] = planeData[4 * k + c];
								}
							}
						}
						return true;
					};

					for (auto& shader : shaders) {
						std::string runError;
						MpvTexture result;
						auto runOnce = [&]() {
							std::map<std::string, MpvTexture> planes;
							planes["LUMA"] = lumaPlane;
							planes["CHROMA"] = chromaPlane;
							if (shader->Run(dev, ctx, "CHROMA", planes, W, H, runError) < 0) {
								return false;
							}
							result = planes["CHROMA"];
							return true;
						};
						double ms = 0;
						if (!timeRuns(runOnce, ms) || result.w != (UINT)W || result.h != (UINT)H) {
							Out("  %-24s failed: %s (%ux%u)\n", shader->Name().c_str(), runError.c_str(), result.w, result.h);
							g_failures++;
							continue;
						}
						std::vector<float> cb, cr;
						if (!readBack(result.tex, &cb, &cr)) {
							g_failures++;
							continue;
						}
						row(shader->Name().c_str(), cb, cr, ms, true);
					}

					// The luma doublers: Cb and Cr each on their own, as the plane the shader
					// hooks. Their passes reuse their textures, so each result is copied out
					// before the next plane goes through.
					if (!doublers.empty()) {
						std::vector<HALF> cbHalf(4 * (size_t)cw * chh), crHalf(4 * (size_t)cw * chh);
						for (size_t k = 0; k < (size_t)cw * chh; k++) {
							cbHalf[4 * k] = cHalf[4 * k];
							crHalf[4 * k] = cHalf[4 * k + 1];
							cbHalf[4 * k + 1] = cbHalf[4 * k + 2] = crHalf[4 * k + 1] = crHalf[4 * k + 2] = zero;
							cbHalf[4 * k + 3] = crHalf[4 * k + 3] = one;
						}
						MpvTexture cbPlane, crPlane, cbOut, crOut;
						if (!MakeMpvTexture(dev, cw, chh, cbPlane, cbHalf.data(), 4 * sizeof(HALF) * cw)
								|| !MakeMpvTexture(dev, cw, chh, crPlane, crHalf.data(), 4 * sizeof(HALF) * cw)
								|| !MakeMpvTexture(dev, W, H, cbOut) || !MakeMpvTexture(dev, W, H, crOut)) {
							Out("  chroma plane textures failed\n");
							g_failures++;
						} else {
							for (auto& shader : doublers) {
								std::string runError;
								auto runOnce = [&]() {
									for (int c = 0; c < 2; c++) {
										std::map<std::string, MpvTexture> planes;
										planes["LUMA"] = c ? crPlane : cbPlane;
										if (shader->Run(dev, ctx, "LUMA", planes, W, H, runError) < 0) {
											return false;
										}
										const MpvTexture& out = planes["LUMA"];
										if (out.w != (UINT)W || out.h != (UINT)H) {
											runError = std::format("left the plane at {}x{}", out.w, out.h);
											return false;
										}
										ctx->CopyResource(c ? crOut.tex : cbOut.tex, out.tex);
									}
									return true;
								};
								double ms = 0;
								if (!timeRuns(runOnce, ms)) {
									Out("  %-24s failed: %s\n", shader->Name().c_str(), runError.c_str());
									g_failures++;
									continue;
								}
								std::vector<float> cb, cr;
								if (!readBack(cbOut.tex, &cb, nullptr) || !readBack(crOut.tex, &cr, nullptr)) {
									g_failures++;
									continue;
								}
								if (shader->HasPixelOffset()) {
									// Where the shader says it left its result, put back.
									ShiftChroma(cb, W, H, shader->PixelOffsetX(), shader->PixelOffsetY());
									ShiftChroma(cr, W, H, shader->PixelOffsetX(), shader->PixelOffsetY());
								}
								row(shader->Name().c_str(), cb, cr, ms, true);

								// The same, pulled back towards the colours the source really
								// carries around each pixel: what a doubler overshoots is
								// ringing, and this is what an AR variant of it would cost.
								std::vector<float> cbAr(cb), crAr(cr);
								for (int j = 0; j < H; j++) {
									for (int i = 0; i < W; i++) {
										const size_t k = (size_t)j * W + i;
										cbAr[k] = AntiRing(cb[k], subCbHalf, cw, chh, i, j, 0.8f);
										crAr[k] = AntiRing(cr[k], subCrHalf, cw, chh, i, j, 0.8f);
									}
								}
								row((shader->Name() + " AR").c_str(), cbAr, crAr, ms, true);
							}
						}
					}

					// Close up where the chroma changes most, in colour.
					std::vector<float> chromaEnergy(4 * (size_t)W * H, 0.0f);
					for (size_t k = 0; k < (size_t)W * H; k++) {
						chromaEnergy[4 * k] = chromaEnergy[4 * k + 1] = chromaEnergy[4 * k + 2] = 4.0f * (refCb[k] + refCr[k]);
					}
					const POINT at = FindDetailWindow(chromaEnergy, W, H, 320, 180);
					auto pick = [&](const char* name) {
						const auto it = kept.find(name);
						return (it == kept.end()) ? nullptr : &it->second;
					};
					const std::wstring board = std::format(L"chroma_{}_{}_zoom.png", ref.name,
						scale == 1 ? L"4k" : scale == 2 ? L"1080p" : L"control");
					SaveZoomBoard(factory, board.c_str(), {
						&rgbaTrim, pick("Bilinear"), pick("Catmull-Rom"),
						pick("KrigBilateral"), pick("CfL_Prediction"), pick("CfL_Prediction_Lite") }, W, H, at);
					if (!doublers.empty()) {
						const std::wstring doublerBoard = std::format(L"chroma_{}_{}_doublers_zoom.png", ref.name,
							scale == 1 ? L"4k" : scale == 2 ? L"1080p" : L"control");
						SaveZoomBoard(factory, doublerBoard.c_str(), {
							&rgbaTrim, pick("Catmull-Rom"), pick("KrigBilateral"),
							pick("FSRCNNX_x2_16-0-4-1"), pick("FSRCNNX_x2_8-0-4-1"), pick("ravu-zoom-ar-r3") }, W, H, at);
					}
				}
			}

			fclose(g_report);
			g_report = nullptr;
			printf("\n  written to temporal_results_chroma.txt, boards saved as chroma_*_zoom.png\n");
			printf("  (each board: reference, Bilinear, Catmull-Rom on top; KrigBilateral, CfL, CfL Lite below;\n");
			printf("  the doublers boards: reference, Catmull-Rom, KrigBilateral on top; FSRCNNX 16, FSRCNNX 8, RAVU-zoom below)\n");
		}
	}
	if (SUCCEEDED(coInit)) {
		CoUninitialize();
	}
	return (rc || g_failures) ? 1 : 0;
}

} // namespace temporal
