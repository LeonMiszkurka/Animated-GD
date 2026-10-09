#pragma once

#include <Geode/Geode.hpp>

namespace afx {
    // Procedurally generated white textures; tint them with setColor().
    enum class Tex { Glow, Ring, DashRing, Spark };

    cocos2d::CCTexture2D* texture(Tex tex);
}
