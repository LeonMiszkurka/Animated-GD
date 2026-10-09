#include "Textures.hpp"

#include <array>
#include <cmath>
#include <functional>
#include <vector>

using namespace geode::prelude;

namespace {
    constexpr float TAU = 6.28318530718f;

    float smoothstep(float e0, float e1, float x) {
        float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    float gauss(float d, float w) {
        float k = d / w;
        return std::exp(-k * k);
    }

    // alpha(x, y) takes coordinates in [-1, 1]
    CCTexture2D* build(int size, std::function<float(float, float)> const& alpha) {
        std::vector<uint8_t> px(size * size * 4);
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                float nx = (x + .5f) / size * 2.f - 1.f;
                float ny = (y + .5f) / size * 2.f - 1.f;
                float a = std::clamp(alpha(nx, ny), 0.f, 1.f);
                auto i = (y * size + x) * 4;
                px[i] = px[i + 1] = px[i + 2] = 255;
                px[i + 3] = static_cast<uint8_t>(a * 255.f);
            }
        }
        auto tex = new CCTexture2D();
        if (!tex->initWithData(
            px.data(), kCCTexture2DPixelFormat_RGBA8888, size, size,
            CCSize(static_cast<float>(size), static_cast<float>(size))
        )) {
            tex->release();
            return nullptr;
        }
        tex->setAntiAliasTexParameters();
        // intentionally kept alive for the whole session
        return tex;
    }

    float ringAlpha(float r) {
        float d = std::abs(r - .78f);
        return (gauss(d, .055f) + .35f * gauss(d, .18f)) * smoothstep(1.f, .93f, r);
    }

    CCTexture2D* create(afx::Tex tex) {
        switch (tex) {
            case afx::Tex::Glow:
                return build(64, [](float x, float y) {
                    float r = std::sqrt(x * x + y * y);
                    return std::pow(std::max(0.f, 1.f - r), 2.2f);
                });
            case afx::Tex::Ring:
                return build(128, [](float x, float y) {
                    return ringAlpha(std::sqrt(x * x + y * y));
                });
            case afx::Tex::DashRing:
                return build(128, [](float x, float y) {
                    float seg = std::atan2(y, x) / TAU * 8.f;
                    seg -= std::floor(seg);
                    float dash = smoothstep(0.f, .07f, seg) * smoothstep(.62f, .55f, seg);
                    return ringAlpha(std::sqrt(x * x + y * y)) * dash;
                });
            case afx::Tex::Spark:
                return build(32, [](float x, float y) {
                    float r = std::sqrt(x * x + y * y);
                    float core = gauss(r, .22f);
                    float star = .8f * gauss(x, .07f) * gauss(y, .65f)
                        + .8f * gauss(y, .07f) * gauss(x, .65f);
                    return (core + star) * smoothstep(1.f, .85f, r);
                });
        }
        return nullptr;
    }
}

CCTexture2D* afx::texture(Tex tex) {
    static std::array<CCTexture2D*, 4> cache {};
    auto& slot = cache[static_cast<size_t>(tex)];
    if (!slot) slot = create(tex);
    return slot;
}
