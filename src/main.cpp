#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/EnhancedGameObject.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include "AnimFX.hpp"

using namespace geode::prelude;

class $modify(AFXPlayLayer, PlayLayer) {
    // runs once every object of the level has been created
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        if (!m_objectLayer || AnimFX::forLayer(this)) return;
        AnimFX::create(this);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (auto fx = AnimFX::forLayer(this)) fx->onReset();
    }
};

// Orbs, pads and portals report their activation through this.
class $modify(AFXEnhancedGameObject, EnhancedGameObject) {
    void activatedByPlayer(PlayerObject* player) {
        EnhancedGameObject::activatedByPlayer(player);
        if (auto fx = AnimFX::get()) fx->onObjectActivated(this);
    }
};

// Backup paths for hits that might skip activatedByPlayer (e.g. multi-activate
// orbs). Each only fires when the player's state really changed, and AnimFX
// debounces duplicates per object.
class $modify(AFXBaseGameLayer, GJBaseGameLayer) {
    void gravBumpPlayer(PlayerObject* player, EffectGameObject* object) {
        bool wasUpsideDown = player->m_isUpsideDown;
        GJBaseGameLayer::gravBumpPlayer(player, object);
        if (player->m_isUpsideDown == wasUpsideDown) return;
        if (auto fx = AnimFX::forLayer(this)) fx->onObjectActivated(object);
    }
};

class $modify(AFXPlayerObject, PlayerObject) {
    void ringJump(RingObject* ring, bool skipCheck) {
        auto yVel = m_yVelocity;
        auto upsideDown = m_isUpsideDown;
        auto dashing = m_isDashing;
        auto pos = this->getPosition();
        PlayerObject::ringJump(ring, skipCheck);
        bool jumped = m_yVelocity != yVel || m_isUpsideDown != upsideDown
            || m_isDashing != dashing || !this->getPosition().equals(pos);
        if (!jumped) return;
        if (auto fx = AnimFX::get()) fx->onObjectActivated(ring);
    }
};
