#pragma once

#include <Geode/Geode.hpp>
#include <unordered_map>
#include <vector>
#include "Textures.hpp"

// One AnimFX node lives inside PlayLayer's object layer and drives every
// effect of the mod: idle overlays on portals/orbs/pads, hit bursts, and
// gamemode transitions. Everything is animated by hand in update() so it
// does not depend on GD's action manager or pause handling.
class AnimFX : public cocos2d::CCNode {
public:
    static AnimFX* create(PlayLayer* layer);
    // The FX node of the active PlayLayer, or nullptr (editor, menus...)
    static AnimFX* get();
    static AnimFX* forLayer(GJBaseGameLayer* layer);

    ~AnimFX() override;

    void update(float dt) override;

    void onReset();
    // Several hooks can report the same hit; repeats are debounced per object.
    void onObjectActivated(GameObject* obj);

private:
    enum class Kind { Portal, SpeedPortal, Orb, Pad };

    struct Config {
        bool enabled = true;
        bool portals = true;
        bool orbs = true;
        bool pads = true;
        bool squash = true;
        bool transitions = true;
        bool pop = true;
        bool flash = true;
        bool modeName = false;
        float flashIntensity = .2f;
        float intensity = 1.f;
        float speed = 1.f;
    };

    struct Tracked {
        geode::Ref<GameObject> obj;
        Kind kind;
        cocos2d::ccColor3B color;
        float seed;
    };

    struct Overlay {
        geode::Ref<GameObject> obj;
        cocos2d::CCNode* root = nullptr;
        cocos2d::CCSprite* halo = nullptr;
        cocos2d::CCSprite* ringA = nullptr;
        cocos2d::CCSprite* ringB = nullptr;
        std::vector<cocos2d::CCSprite*> sparks;
        unsigned seenFrame = 0;
        float flareTime = -100.f;
    };

    struct Particle {
        cocos2d::CCNode* node;
        cocos2d::CCRGBAProtocol* rgba;
        cocos2d::CCPoint pos;
        cocos2d::CCPoint vel;
        float age = 0.f;
        float life = .5f;
        float sx0, sy0, sx1, sy1;
        float alpha = 255.f;
        float drag = 1.f;     // velocity multiplier per second
        float fadePow = 1.f;
        float spin = 0.f;     // degrees per second
        bool streak = false;  // align rotation to velocity
    };

    struct Squash {
        geode::Ref<GameObject> obj;
        float start;
        float baseX;
        float baseY;
        bool pad;
    };

    struct PlayerState {
        bool valid = false;
        bool visible = false;
        int mode = 0;
        bool upsideDown = false;
        bool mini = false;
        float popStart = -1.f;
        float popBase = 1.f;
    };

    bool init(PlayLayer* layer);

    void readConfig();
    void syncObjects();
    void updateOverlays();
    void updateParticles(float dt);
    void updateSquashes();
    void updatePlayers();
    void clearOverlays();

    bool kindEnabled(Kind kind) const;
    bool classify(GameObject* obj, Kind& kind, cocos2d::ccColor3B& color) const;
    cocos2d::CCPoint toLocal(cocos2d::CCNode* node);
    cocos2d::CCPoint objectUp(GameObject* obj) const;

    void buildOverlay(Overlay& ov, Tracked const& t);
    void animateOverlay(Overlay& ov, Tracked const& t, cocos2d::CCPoint const& pos, float alpha);

    // effects
    cocos2d::CCSprite* sprite(afx::Tex tex, cocos2d::ccColor3B color);
    Particle* spawn(afx::Tex tex, cocos2d::ccColor3B color, cocos2d::CCPoint pos, float w0, float h0, float w1, float h1, float life);
    void ringBurst(cocos2d::CCPoint pos, cocos2d::ccColor3B color, float d0, float d1, float life, float yRatio = 1.f, float rotation = 0.f, float alpha = 255.f, afx::Tex tex = afx::Tex::Ring);
    void sparkBurst(cocos2d::CCPoint pos, cocos2d::ccColor3B color, int count, float minSpeed, float maxSpeed, float dirDeg, float spreadDeg, float life);
    void implode(cocos2d::CCPoint pos, cocos2d::ccColor3B color, int count, float radius, float life);
    void startSquash(GameObject* obj, bool pad);

    bool debounce(GameObject* obj);
    void orbHit(GameObject* obj, cocos2d::ccColor3B color);
    void padHit(GameObject* obj, cocos2d::ccColor3B color);
    void portalFlare(GameObject* obj, Kind kind, cocos2d::ccColor3B color);

    void modeTransition(PlayerObject* player, PlayerState& state, int oldMode, int newMode);
    void gravityTransition(PlayerObject* player, bool upsideDown);
    void sizeTransition(PlayerObject* player, bool mini);

    PlayLayer* m_layer = nullptr;
    cocos2d::CCNode* m_overlayLayer = nullptr;
    cocos2d::CCNode* m_burstLayer = nullptr;
    cocos2d::CCLayerColor* m_flash = nullptr;
    float m_flashAlpha = 0.f;

    Config m_cfg;
    float m_time = 0.f;      // idle animation clock (scaled by speed)
    float m_realTime = 0.f;  // real clock for bursts/squashes
    float m_resetTime = 0.f;
    unsigned m_frame = 0;

    unsigned m_scanned = 0;
    std::vector<Tracked> m_tracked;
    std::unordered_map<GameObject*, Overlay> m_live;
    std::unordered_map<cocos2d::CCNode*, cocos2d::CCAffineTransform> m_xforms;
    std::vector<Particle> m_particles;
    std::unordered_map<GameObject*, Squash> m_squashes;
    std::unordered_map<GameObject*, float> m_lastHit;
    PlayerState m_players[2];
};
