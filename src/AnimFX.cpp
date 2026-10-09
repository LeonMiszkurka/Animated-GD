#include "AnimFX.hpp"

#include <algorithm>
#include <cmath>
#include <random>

using namespace geode::prelude;
using afx::Tex;

namespace {
    constexpr float PI = 3.14159265359f;
    constexpr float TAU = 6.28318530718f;
    constexpr size_t MAX_PARTICLES = 900;

    AnimFX* s_current = nullptr;

    // cube, ship, ball, ufo, wave, robot, spider, swing
    constexpr ccColor3B MODE_COLORS[] = {
        { 90, 255, 110 },
        { 255, 90, 220 },
        { 255, 110, 60 },
        { 255, 200, 40 },
        { 60, 200, 255 },
        { 235, 240, 255 },
        { 175, 90, 255 },
        { 255, 230, 70 },
    };
    constexpr char const* MODE_NAMES[] = {
        "CUBE", "SHIP", "BALL", "UFO", "WAVE", "ROBOT", "SPIDER", "SWING",
    };

    constexpr ccColor3B GRAVITY_NORMAL { 60, 200, 255 };
    constexpr ccColor3B GRAVITY_INVERSE { 255, 220, 60 };
    constexpr ccColor3B SIZE_MINI { 255, 90, 220 };
    constexpr ccColor3B SIZE_BIG { 90, 255, 110 };

    float frand(float lo, float hi) {
        static std::mt19937 rng { std::random_device {}() };
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    }

    float fract(float v) {
        return v - std::floor(v);
    }

    GLubyte toByte(float v) {
        return static_cast<GLubyte>(std::clamp(v, 0.f, 255.f));
    }

    ccColor3B lighten(ccColor3B c, float t = .45f) {
        return {
            toByte(c.r + (255 - c.r) * t),
            toByte(c.g + (255 - c.g) * t),
            toByte(c.b + (255 - c.b) * t),
        };
    }

    void sizeTo(CCSprite* spr, float w, float h) {
        auto cs = spr->getContentSize();
        spr->setScaleX(w / cs.width);
        spr->setScaleY(h / cs.height);
    }

    CCAffineTransform concat(CCAffineTransform const& t1, CCAffineTransform const& t2) {
        return {
            t1.a * t2.a + t1.b * t2.c,
            t1.a * t2.b + t1.b * t2.d,
            t1.c * t2.a + t1.d * t2.c,
            t1.c * t2.b + t1.d * t2.d,
            t1.tx * t2.a + t1.ty * t2.c + t2.tx,
            t1.tx * t2.b + t1.ty * t2.d + t2.ty,
        };
    }

    CCPoint apply(CCPoint const& p, CCAffineTransform const& t) {
        return { t.a * p.x + t.c * p.y + t.tx, t.b * p.x + t.d * p.y + t.ty };
    }

    int modeOf(PlayerObject* p) {
        if (p->m_isShip) return 1;
        if (p->m_isBall) return 2;
        if (p->m_isBird) return 3;
        if (p->m_isDart) return 4;
        if (p->m_isRobot) return 5;
        if (p->m_isSpider) return 6;
        if (p->m_isSwing) return 7;
        return 0;
    }

    // base sizes in object units (before the object's own scale)
    constexpr float PORTAL_W = 34.f, PORTAL_H = 86.f;
    constexpr float SPEED_W = 36.f, SPEED_H = 44.f;
    constexpr float ORB_D = 36.f;
    constexpr float PAD_W = 32.f, PAD_H = 8.f;
}

// ---------------------------------------------------------------------------
// lifecycle

AnimFX* AnimFX::create(PlayLayer* layer) {
    auto ret = new AnimFX();
    if (ret->init(layer)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

AnimFX* AnimFX::get() {
    auto pl = PlayLayer::get();
    if (s_current && pl && s_current->m_layer == pl) return s_current;
    return nullptr;
}

AnimFX* AnimFX::forLayer(GJBaseGameLayer* layer) {
    if (s_current && layer && static_cast<GJBaseGameLayer*>(s_current->m_layer) == layer) return s_current;
    return nullptr;
}

AnimFX::~AnimFX() {
    if (s_current == this) s_current = nullptr;
}

bool AnimFX::init(PlayLayer* layer) {
    if (!CCNode::init()) return false;
    m_layer = layer;
    s_current = this;
    this->setID("anim-fx"_spr);

    auto objectLayer = layer->m_objectLayer;

    // idle overlays sit just below the player, bursts just above
    int playerZ = 1000;
    if (layer->m_player1 && layer->m_player1->getParent() == objectLayer) {
        playerZ = layer->m_player1->getZOrder();
    }
    m_overlayLayer = CCNode::create();
    m_overlayLayer->setID("anim-fx-overlays"_spr);
    objectLayer->addChild(m_overlayLayer, playerZ - 1);

    m_burstLayer = CCNode::create();
    this->addChild(m_burstLayer);
    objectLayer->addChild(this, playerZ + 1);

    auto win = CCDirector::get()->getWinSize();
    m_flash = CCLayerColor::create({ 255, 255, 255, 0 }, win.width, win.height);
    m_flash->setBlendFunc({ GL_SRC_ALPHA, GL_ONE });
    m_flash->setID("anim-fx-flash"_spr);
    int flashZ = layer->m_uiLayer ? layer->m_uiLayer->getZOrder() - 1 : 100;
    layer->addChild(m_flash, flashZ);

    readConfig();
    this->scheduleUpdate();
    return true;
}

void AnimFX::readConfig() {
    auto mod = Mod::get();
    m_cfg.enabled = mod->getSettingValue<bool>("enabled");
    m_cfg.portals = mod->getSettingValue<bool>("animate-portals");
    m_cfg.orbs = mod->getSettingValue<bool>("animate-orbs");
    m_cfg.pads = mod->getSettingValue<bool>("animate-pads");
    m_cfg.squash = mod->getSettingValue<bool>("squash-objects");
    m_cfg.transitions = mod->getSettingValue<bool>("mode-transitions");
    m_cfg.pop = mod->getSettingValue<bool>("player-pop");
    m_cfg.flash = mod->getSettingValue<bool>("screen-flash");
    m_cfg.modeName = mod->getSettingValue<bool>("show-mode-name");
    m_cfg.flashIntensity = static_cast<float>(mod->getSettingValue<double>("flash-intensity"));
    m_cfg.intensity = static_cast<float>(mod->getSettingValue<double>("intensity"));
    m_cfg.speed = static_cast<float>(mod->getSettingValue<double>("animation-speed"));
}

// ---------------------------------------------------------------------------
// frame loop

void AnimFX::update(float dt) {
    dt = std::min(dt, .1f);
    m_frame++;
    m_realTime += dt;
    m_xforms.clear();
    readConfig();

    if (!m_cfg.enabled) {
        if (!m_live.empty()) clearOverlays();
    }
    else {
        m_time += dt * m_cfg.speed;
        syncObjects();
        updateOverlays();
        updatePlayers();
    }
    updateParticles(dt);
    updateSquashes();

    if (m_flash) {
        m_flashAlpha = std::max(0.f, m_flashAlpha - dt * 3.5f);
        m_flash->setOpacity(toByte(m_flashAlpha * m_flashAlpha * 255.f * m_cfg.flashIntensity));
    }
}

void AnimFX::onReset() {
    m_resetTime = m_realTime;
    for (int i = 0; i < 2; i++) {
        auto& st = m_players[i];
        auto p = i == 0 ? m_layer->m_player1 : m_layer->m_player2;
        if (st.popStart >= 0.f && p && p->m_mainLayer) {
            p->m_mainLayer->setScale(st.popBase);
        }
        st = PlayerState();
    }
    for (auto& p : m_particles) p.node->removeFromParent();
    m_particles.clear();
    for (auto& [_, sq] : m_squashes) {
        sq.obj->CCSprite::setScaleX(sq.baseX);
        sq.obj->CCSprite::setScaleY(sq.baseY);
    }
    m_squashes.clear();
    m_lastHit.clear();
    for (auto& [_, ov] : m_live) ov.flareTime = -100.f;
    m_flashAlpha = 0.f;
}

// ---------------------------------------------------------------------------
// object tracking

bool AnimFX::classify(GameObject* obj, Kind& kind, ccColor3B& color) const {
    switch (obj->m_objectID) {
        case 200: kind = Kind::SpeedPortal; color = { 255, 180, 40 }; return true;
        case 201: kind = Kind::SpeedPortal; color = { 60, 180, 255 }; return true;
        case 202: kind = Kind::SpeedPortal; color = { 90, 255, 110 }; return true;
        case 203: kind = Kind::SpeedPortal; color = { 255, 90, 220 }; return true;
        case 1334: kind = Kind::SpeedPortal; color = { 255, 70, 70 }; return true;
        default: break;
    }

    using T = GameObjectType;
    switch (obj->m_objectType) {
        case T::CubePortal: kind = Kind::Portal; color = MODE_COLORS[0]; return true;
        case T::ShipPortal: kind = Kind::Portal; color = MODE_COLORS[1]; return true;
        case T::BallPortal: kind = Kind::Portal; color = MODE_COLORS[2]; return true;
        case T::UfoPortal: kind = Kind::Portal; color = MODE_COLORS[3]; return true;
        case T::WavePortal: kind = Kind::Portal; color = MODE_COLORS[4]; return true;
        case T::RobotPortal: kind = Kind::Portal; color = MODE_COLORS[5]; return true;
        case T::SpiderPortal: kind = Kind::Portal; color = MODE_COLORS[6]; return true;
        case T::SwingPortal: kind = Kind::Portal; color = MODE_COLORS[7]; return true;
        case T::NormalGravityPortal: kind = Kind::Portal; color = GRAVITY_NORMAL; return true;
        case T::InverseGravityPortal: kind = Kind::Portal; color = GRAVITY_INVERSE; return true;
        case T::GravityTogglePortal: kind = Kind::Portal; color = { 90, 255, 140 }; return true;
        case T::RegularSizePortal: kind = Kind::Portal; color = SIZE_BIG; return true;
        case T::MiniSizePortal: kind = Kind::Portal; color = SIZE_MINI; return true;
        case T::NormalMirrorPortal: kind = Kind::Portal; color = { 60, 150, 255 }; return true;
        case T::InverseMirrorPortal: kind = Kind::Portal; color = { 255, 150, 40 }; return true;
        case T::DualPortal: kind = Kind::Portal; color = { 255, 140, 40 }; return true;
        case T::SoloPortal: kind = Kind::Portal; color = { 60, 180, 255 }; return true;
        case T::TeleportPortal: kind = Kind::Portal; color = { 255, 150, 40 }; return true;

        case T::YellowJumpRing: kind = Kind::Orb; color = { 255, 230, 40 }; return true;
        case T::PinkJumpRing: kind = Kind::Orb; color = { 255, 90, 220 }; return true;
        case T::RedJumpRing: kind = Kind::Orb; color = { 255, 60, 60 }; return true;
        case T::GravityRing: kind = Kind::Orb; color = { 60, 200, 255 }; return true;
        case T::GreenRing: kind = Kind::Orb; color = { 60, 255, 100 }; return true;
        case T::DropRing: kind = Kind::Orb; color = { 140, 110, 255 }; return true;
        case T::CustomRing: kind = Kind::Orb; color = { 255, 255, 255 }; return true;
        case T::DashRing: kind = Kind::Orb; color = { 80, 255, 140 }; return true;
        case T::GravityDashRing: kind = Kind::Orb; color = { 255, 80, 200 }; return true;
        case T::SpiderOrb: kind = Kind::Orb; color = { 190, 80, 255 }; return true;
        case T::TeleportOrb: kind = Kind::Orb; color = { 90, 255, 240 }; return true;

        case T::YellowJumpPad: kind = Kind::Pad; color = { 255, 230, 40 }; return true;
        case T::PinkJumpPad: kind = Kind::Pad; color = { 255, 90, 220 }; return true;
        case T::RedJumpPad: kind = Kind::Pad; color = { 255, 60, 60 }; return true;
        case T::GravityPad: kind = Kind::Pad; color = { 60, 200, 255 }; return true;
        case T::SpiderPad: kind = Kind::Pad; color = { 190, 80, 255 }; return true;

        default: return false;
    }
}

bool AnimFX::kindEnabled(Kind kind) const {
    switch (kind) {
        case Kind::Portal:
        case Kind::SpeedPortal: return m_cfg.portals;
        case Kind::Orb: return m_cfg.orbs;
        case Kind::Pad: return m_cfg.pads;
    }
    return false;
}

void AnimFX::syncObjects() {
    auto arr = m_layer->m_objects;
    if (!arr) return;
    unsigned count = arr->count();
    if (count < m_scanned) {
        // the object list was rebuilt; start over
        clearOverlays();
        m_tracked.clear();
        m_scanned = 0;
    }
    for (unsigned i = m_scanned; i < count; i++) {
        auto obj = typeinfo_cast<GameObject*>(arr->objectAtIndex(i));
        if (!obj) continue;
        Kind kind;
        ccColor3B color;
        if (classify(obj, kind, color)) {
            m_tracked.push_back({ obj, kind, color, frand(0.f, TAU) });
        }
    }
    m_scanned = count;
}

CCPoint AnimFX::toLocal(CCNode* node) {
    auto parent = node->getParent();
    if (!parent) return node->getPosition();
    auto it = m_xforms.find(parent);
    if (it == m_xforms.end()) {
        auto xf = concat(parent->nodeToWorldTransform(), this->worldToNodeTransform());
        it = m_xforms.emplace(parent, xf).first;
    }
    return apply(node->getPosition(), it->second);
}

CCPoint AnimFX::objectUp(GameObject* obj) const {
    float r = obj->getRotation() * PI / 180.f;
    CCPoint up { std::sin(r), std::cos(r) };
    return obj->m_isFlipY ? CCPoint { -up.x, -up.y } : up;
}

void AnimFX::clearOverlays() {
    for (auto& [_, ov] : m_live) ov.root->removeFromParent();
    m_live.clear();
}

// ---------------------------------------------------------------------------
// idle overlays

CCSprite* AnimFX::sprite(Tex tex, ccColor3B color) {
    auto spr = CCSprite::createWithTexture(afx::texture(tex));
    spr->setBlendFunc({ GL_SRC_ALPHA, GL_ONE });
    spr->setColor(color);
    return spr;
}

void AnimFX::buildOverlay(Overlay& ov, Tracked const& t) {
    ov.obj = t.obj;
    ov.root = CCNode::create();
    m_overlayLayer->addChild(ov.root);

    auto bright = lighten(t.color, .35f);
    ov.halo = sprite(Tex::Glow, t.color);
    ov.root->addChild(ov.halo);

    int sparks = 0;
    switch (t.kind) {
        case Kind::Portal:
            ov.ringA = sprite(Tex::Ring, t.color);
            ov.ringB = sprite(Tex::Ring, t.color);
            sparks = 8;
            break;
        case Kind::SpeedPortal:
            ov.ringA = sprite(Tex::Ring, t.color);
            sparks = 6;
            break;
        case Kind::Orb:
            ov.ringA = sprite(Tex::DashRing, bright);
            ov.ringB = sprite(Tex::Ring, t.color);
            sparks = 3;
            break;
        case Kind::Pad:
            ov.ringA = sprite(Tex::Ring, t.color);
            sparks = 5;
            break;
    }
    if (ov.ringA) ov.root->addChild(ov.ringA);
    if (ov.ringB) ov.root->addChild(ov.ringB);
    for (int i = 0; i < sparks; i++) {
        auto spr = sprite(Tex::Spark, bright);
        ov.root->addChild(spr);
        ov.sparks.push_back(spr);
    }
}

void AnimFX::updateOverlays() {
    auto win = CCDirector::get()->getWinSize();
    CCPoint corners[] = {
        this->convertToNodeSpace({ 0, 0 }),
        this->convertToNodeSpace({ win.width, 0 }),
        this->convertToNodeSpace({ 0, win.height }),
        this->convertToNodeSpace({ win.width, win.height }),
    };
    float minX = corners[0].x, maxX = corners[0].x, minY = corners[0].y, maxY = corners[0].y;
    for (auto& c : corners) {
        minX = std::min(minX, c.x); maxX = std::max(maxX, c.x);
        minY = std::min(minY, c.y); maxY = std::max(maxY, c.y);
    }
    constexpr float margin = 120.f;
    minX -= margin; maxX += margin; minY -= margin; maxY += margin;

    for (auto const& t : m_tracked) {
        if (!kindEnabled(t.kind)) continue;
        GameObject* obj = t.obj;
        if (!obj->getParent() || !obj->isVisible() || obj->m_isHide) continue;

        auto pos = toLocal(obj);
        if (pos.x < minX || pos.x > maxX || pos.y < minY || pos.y > maxY) continue;

        float alpha = obj->getOpacity() / 255.f * m_cfg.intensity;
        if (alpha <= .01f) continue;

        auto& ov = m_live[obj];
        if (!ov.root) buildOverlay(ov, t);
        ov.seenFrame = m_frame;
        animateOverlay(ov, t, pos, alpha);
    }

    for (auto it = m_live.begin(); it != m_live.end();) {
        if (it->second.seenFrame != m_frame) {
            it->second.root->removeFromParent();
            it = m_live.erase(it);
        }
        else ++it;
    }
}

void AnimFX::animateOverlay(Overlay& ov, Tracked const& t, CCPoint const& pos, float a) {
    GameObject* obj = t.obj;
    auto root = ov.root;
    root->setPosition(pos);
    root->setRotation(obj->getRotation());
    root->setScaleX(std::abs(obj->getScaleX()) * (obj->m_isFlipX ? -1.f : 1.f));
    root->setScaleY(std::abs(obj->getScaleY()) * (obj->m_isFlipY ? -1.f : 1.f));

    float time = m_time + t.seed;
    float fq = (m_realTime - ov.flareTime) / .6f;
    float boost = fq < 1.f ? (1.f - fq) * (1.f - fq) : 0.f;
    auto op = [a](float v) { return toByte(v * a); };

    switch (t.kind) {
        case Kind::Portal: {
            // breathing aura
            float pulse = (1.f + .06f * std::sin(time * 2.4f)) * (1.f + .5f * boost);
            sizeTo(ov.halo, PORTAL_W * 1.9f * pulse, PORTAL_H * 1.3f * pulse);
            ov.halo->setOpacity(op(105.f + 40.f * std::sin(time * 2.4f) + 120.f * boost));

            // energy ripples flowing out of the portal mouth
            CCSprite* rings[] = { ov.ringA, ov.ringB };
            for (int i = 0; i < 2; i++) {
                float p = fract(time * .65f + i * .5f);
                sizeTo(rings[i], PORTAL_W * (.7f + .9f * p), PORTAL_H * (.8f + .45f * p));
                rings[i]->setOpacity(op(170.f * std::pow(1.f - p, 1.5f)));
            }

            // sparks spinning around the portal like a vortex seen edge-on
            int n = static_cast<int>(ov.sparks.size());
            for (int i = 0; i < n; i++) {
                float ang = time * 2.f + i * TAU / n;
                float depth = (std::sin(ang) + 1.f) * .5f;
                auto spr = ov.sparks[i];
                spr->setPosition({ std::sin(ang) * PORTAL_W * .3f, std::cos(ang) * PORTAL_H * .46f });
                float s = 4.f + 5.f * depth;
                sizeTo(spr, s, s);
                spr->setOpacity(op(70.f + 170.f * depth));
            }
            break;
        }
        case Kind::SpeedPortal: {
            float pulse = (1.f + .07f * std::sin(time * 3.f)) * (1.f + .5f * boost);
            sizeTo(ov.halo, SPEED_W * 1.7f * pulse, SPEED_H * 1.4f * pulse);
            ov.halo->setOpacity(op(100.f + 40.f * std::sin(time * 3.f) + 120.f * boost));

            float p = fract(time * .8f);
            sizeTo(ov.ringA, SPEED_W * (.6f + .8f * p), SPEED_H * (.6f + .8f * p));
            ov.ringA->setOpacity(op(150.f * std::pow(1.f - p, 1.5f)));

            // streaks rushing forward through the portal
            int n = static_cast<int>(ov.sparks.size());
            for (int i = 0; i < n; i++) {
                float q = fract(time * 1.4f + static_cast<float>(i) / n);
                float lane = (i % 3 - 1) * SPEED_H * .28f + std::sin(i * 2.3f) * 3.f;
                auto spr = ov.sparks[i];
                spr->setPosition({ -SPEED_W * .9f + q * SPEED_W * 1.8f, lane });
                float bell = std::sin(PI * q);
                sizeTo(spr, 6.f + 14.f * bell, 3.5f);
                spr->setOpacity(op(230.f * bell));
            }
            break;
        }
        case Kind::Orb: {
            float pulse = (1.f + .1f * std::sin(time * 2.8f)) * (1.f + .7f * boost);
            float haloD = ORB_D * 1.75f * pulse;
            sizeTo(ov.halo, haloD, haloD);
            ov.halo->setOpacity(op(120.f + 45.f * std::sin(time * 2.8f) + 120.f * boost));

            // spinning dashed ring
            float ringD = ORB_D * 1.32f * (1.f + .03f * std::sin(time * 6.f)) * (1.f + .3f * boost);
            sizeTo(ov.ringA, ringD, ringD);
            ov.ringA->setRotation(m_time * 120.f + t.seed * 57.f);
            ov.ringA->setOpacity(op(190.f));

            // ripple
            float p = fract(time * .85f);
            float rippleD = ORB_D * (1.05f + .85f * p);
            sizeTo(ov.ringB, rippleD, rippleD);
            ov.ringB->setOpacity(op(170.f * std::pow(1.f - p, 1.6f)));

            // orbiting sparks
            int n = static_cast<int>(ov.sparks.size());
            for (int i = 0; i < n; i++) {
                float ang = time * 2.6f + i * TAU / n;
                auto spr = ov.sparks[i];
                spr->setPosition({ std::cos(ang) * ORB_D * .78f, std::sin(ang) * ORB_D * .78f });
                float s = 7.f * (.85f + .25f * std::sin(time * 7.f + i));
                sizeTo(spr, s, s);
                spr->setOpacity(op(220.f));
            }
            break;
        }
        case Kind::Pad: {
            float pulse = (1.f + .08f * std::sin(time * 3.2f)) * (1.f + .6f * boost);
            sizeTo(ov.halo, PAD_W * 1.7f * pulse, PAD_H * 3.4f * pulse);
            ov.halo->setPosition({ 0.f, 2.f });
            ov.halo->setOpacity(op(110.f + 40.f * std::sin(time * 3.2f) + 120.f * boost));

            float p = fract(time * 1.1f);
            sizeTo(ov.ringA, PAD_W * (.7f + .9f * p), PAD_H * (1.2f + 2.4f * p));
            ov.ringA->setPosition({ 0.f, 1.f });
            ov.ringA->setOpacity(op(160.f * std::pow(1.f - p, 1.5f)));

            // energy rising off the pad
            int n = static_cast<int>(ov.sparks.size());
            for (int i = 0; i < n; i++) {
                float q = fract(time * .95f + static_cast<float>(i) / n);
                float lane = (fract(std::sin(i * 12.9898f + t.seed) * 43758.5453f) - .5f) * PAD_W * .8f;
                auto spr = ov.sparks[i];
                spr->setPosition({ lane + std::sin(time * 3.f + i * 1.7f) * 2.f, 2.f + q * 30.f });
                float s = 6.f * (1.f - .5f * q);
                sizeTo(spr, s, s);
                spr->setOpacity(op(230.f * std::sin(PI * q)));
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// particles / bursts

AnimFX::Particle* AnimFX::spawn(
    Tex tex, ccColor3B color, CCPoint pos, float w0, float h0, float w1, float h1, float life
) {
    if (m_particles.size() >= MAX_PARTICLES) return nullptr;
    auto spr = sprite(tex, color);
    auto cs = spr->getContentSize();
    spr->setPosition(pos);
    m_burstLayer->addChild(spr);

    Particle p;
    p.node = spr;
    p.rgba = spr;
    p.pos = pos;
    p.vel = CCPoint(0, 0);
    p.life = life;
    p.sx0 = w0 / cs.width;
    p.sy0 = h0 / cs.height;
    p.sx1 = w1 / cs.width;
    p.sy1 = h1 / cs.height;
    p.alpha = 255.f * std::min(m_cfg.intensity, 1.f);
    spr->setScaleX(p.sx0);
    spr->setScaleY(p.sy0);
    m_particles.push_back(p);
    return &m_particles.back();
}

void AnimFX::updateParticles(float dt) {
    for (size_t i = 0; i < m_particles.size();) {
        auto& p = m_particles[i];
        p.age += dt;
        if (p.age >= p.life) {
            p.node->removeFromParent();
            p = m_particles.back();
            m_particles.pop_back();
            continue;
        }
        float q = p.age / p.life;
        float e = 1.f - std::pow(2.f, -10.f * q);

        if (p.drag != 1.f) {
            float k = std::pow(p.drag, dt);
            p.vel = CCPoint(p.vel.x * k, p.vel.y * k);
        }
        p.pos = CCPoint(p.pos.x + p.vel.x * dt, p.pos.y + p.vel.y * dt);
        p.node->setPosition(p.pos);
        p.node->setScaleX(p.sx0 + (p.sx1 - p.sx0) * e);
        p.node->setScaleY(p.sy0 + (p.sy1 - p.sy0) * e);

        if (p.streak && (std::abs(p.vel.x) + std::abs(p.vel.y)) > 1.f) {
            p.node->setRotation(-std::atan2(p.vel.y, p.vel.x) * 180.f / PI);
        }
        else if (p.spin != 0.f) {
            p.node->setRotation(p.node->getRotation() + p.spin * dt);
        }
        p.rgba->setOpacity(toByte(p.alpha * std::pow(1.f - q, p.fadePow)));
        i++;
    }
}

void AnimFX::ringBurst(
    CCPoint pos, ccColor3B color, float d0, float d1, float life,
    float yRatio, float rotation, float alpha, Tex tex
) {
    auto p = spawn(tex, color, pos, d0, d0 * yRatio, d1, d1 * yRatio, life);
    if (!p) return;
    p->alpha = alpha * std::min(m_cfg.intensity, 1.f);
    p->fadePow = 1.4f;
    p->node->setRotation(rotation);
    if (tex == Tex::DashRing) p->spin = frand(-260.f, 260.f);
}

void AnimFX::sparkBurst(
    CCPoint pos, ccColor3B color, int count, float minSpeed, float maxSpeed,
    float dirDeg, float spreadDeg, float life
) {
    auto bright = lighten(color, .5f);
    for (int i = 0; i < count; i++) {
        float ang = (dirDeg + frand(-spreadDeg, spreadDeg) * .5f) * PI / 180.f;
        float speed = frand(minSpeed, maxSpeed);
        float len = frand(14.f, 24.f);
        auto p = spawn(
            Tex::Spark, i % 3 == 0 ? bright : color, pos,
            len, 5.f, 3.f, 2.f, life * frand(.7f, 1.15f)
        );
        if (!p) return;
        p->vel = CCPoint(std::cos(ang) * speed, std::sin(ang) * speed);
        p->drag = .03f;
        p->streak = true;
        p->fadePow = 1.2f;
    }
}

void AnimFX::implode(CCPoint pos, ccColor3B color, int count, float radius, float life) {
    for (int i = 0; i < count; i++) {
        float ang = i * TAU / count + frand(-.2f, .2f);
        CCPoint dir { std::cos(ang), std::sin(ang) };
        auto p = spawn(
            Tex::Spark, color, { pos.x + dir.x * radius, pos.y + dir.y * radius },
            16.f, 5.f, 4.f, 2.f, life
        );
        if (!p) return;
        p->vel = CCPoint(-dir.x * radius / life, -dir.y * radius / life);
        p->streak = true;
        p->fadePow = .6f;
    }
}

void AnimFX::startSquash(GameObject* obj, bool pad) {
    if (!m_cfg.squash) return;
    auto it = m_squashes.find(obj);
    if (it != m_squashes.end()) {
        it->second.start = m_realTime;
        return;
    }
    m_squashes.emplace(obj, Squash { obj, m_realTime, obj->getScaleX(), obj->getScaleY(), pad });
}

void AnimFX::updateSquashes() {
    for (auto it = m_squashes.begin(); it != m_squashes.end();) {
        auto& sq = it->second;
        float tau = m_realTime - sq.start;
        float dur = sq.pad ? .55f : .5f;
        // the CCSprite:: calls only change the visual scale, never GD's hitbox data
        if (tau >= dur) {
            sq.obj->CCSprite::setScaleX(sq.baseX);
            sq.obj->CCSprite::setScaleY(sq.baseY);
            it = m_squashes.erase(it);
            continue;
        }
        if (sq.pad) {
            float w = std::exp(-7.f * tau) * std::cos(15.f * tau);
            sq.obj->CCSprite::setScaleX(sq.baseX * (1.f + .3f * w));
            sq.obj->CCSprite::setScaleY(sq.baseY * (1.f - .6f * w));
        }
        else {
            float f = 1.f - .3f * std::exp(-8.f * tau) * std::cos(16.f * tau);
            sq.obj->CCSprite::setScaleX(sq.baseX * f);
            sq.obj->CCSprite::setScaleY(sq.baseY * f);
        }
        ++it;
    }
}

// ---------------------------------------------------------------------------
// hit events

bool AnimFX::debounce(GameObject* obj) {
    auto it = m_lastHit.find(obj);
    if (it != m_lastHit.end() && m_realTime - it->second < .1f) return true;
    m_lastHit[obj] = m_realTime;
    return false;
}

void AnimFX::onObjectActivated(GameObject* obj) {
    if (!m_cfg.enabled || !obj) return;
    Kind kind;
    ccColor3B color;
    if (!classify(obj, kind, color) || !kindEnabled(kind)) return;
    if (debounce(obj)) return;

    switch (kind) {
        case Kind::Orb: orbHit(obj, color); break;
        case Kind::Pad: padHit(obj, color); break;
        case Kind::Portal:
        case Kind::SpeedPortal: portalFlare(obj, kind, color); break;
    }
    auto ov = m_live.find(obj);
    if (ov != m_live.end()) ov->second.flareTime = m_realTime;
}

void AnimFX::orbHit(GameObject* obj, ccColor3B color) {
    auto pos = toLocal(obj);
    float d = ORB_D * std::abs(obj->getScaleX());

    ringBurst(pos, color, d * .9f, d * 3.4f, .45f);
    ringBurst(pos, lighten(color), d * .6f, d * 2.1f, .28f, 1.f, frand(0.f, 360.f), 220.f, Tex::DashRing);
    if (auto flash = spawn(Tex::Glow, color, pos, d * 1.4f, d * 1.4f, d * 3.f, d * 3.f, .25f)) {
        flash->alpha *= .9f;
    }
    sparkBurst(pos, color, 12, 140.f, 330.f, 0.f, 360.f, .45f);
    startSquash(obj, false);
}

void AnimFX::padHit(GameObject* obj, ccColor3B color) {
    auto pos = toLocal(obj);
    auto up = objectUp(obj);
    float w = PAD_W * std::abs(obj->getScaleX());
    float dirDeg = std::atan2(up.y, up.x) * 180.f / PI;
    CCPoint base { pos.x + up.x * 2.f, pos.y + up.y * 2.f };

    ringBurst(base, color, w * .6f, w * 2.6f, .4f, .32f, obj->getRotation());
    if (auto flash = spawn(Tex::Glow, color, base, w * 1.2f, w * .6f, w * 2.6f, w * 1.4f, .25f)) {
        flash->node->setRotation(obj->getRotation());
    }
    sparkBurst(base, color, 14, 220.f, 520.f, dirDeg, 40.f, .5f);
    startSquash(obj, true);
}

void AnimFX::portalFlare(GameObject* obj, Kind kind, ccColor3B color) {
    auto pos = toLocal(obj);
    float s = std::abs(obj->getScaleY());
    if (kind == Kind::SpeedPortal) {
        ringBurst(pos, color, 30.f * s, 110.f * s, .45f);
        sparkBurst(pos, color, 8, 160.f, 360.f, obj->m_isFlipX ? 180.f : 0.f, 50.f, .4f);
    }
    else {
        ringBurst(pos, color, 40.f * s, 120.f * s, .5f, 2.4f, obj->getRotation(), 220.f);
        sparkBurst(pos, color, 8, 100.f, 260.f, 0.f, 360.f, .4f);
    }
}

// ---------------------------------------------------------------------------
// gamemode transitions

void AnimFX::updatePlayers() {
    bool canFx = m_cfg.transitions && m_realTime - m_resetTime > .15f;
    PlayerObject* players[] = { m_layer->m_player1, m_layer->m_player2 };

    for (int i = 0; i < 2; i++) {
        auto p = players[i];
        auto& st = m_players[i];
        if (!p) continue;

        // elastic morph pop on the icon
        if (st.popStart >= 0.f && p->m_mainLayer) {
            float tau = m_realTime - st.popStart;
            if (tau >= .6f) {
                p->m_mainLayer->setScale(st.popBase);
                st.popStart = -1.f;
            }
            else {
                float f = 1.f - .55f * std::exp(-7.f * tau) * std::cos(14.f * tau);
                p->m_mainLayer->setScale(st.popBase * f);
            }
        }

        if (p->m_isDead) continue;
        if (!p->isVisible()) {
            st.valid = false;
            st.visible = false;
            continue;
        }

        int mode = modeOf(p);
        bool upside = p->m_isUpsideDown;
        bool mini = p->m_vehicleSize < .99f;

        if (!st.valid) {
            bool appeared = i == 1 && !st.visible && canFx && m_realTime - m_resetTime > .5f;
            st.valid = true;
            st.visible = true;
            st.mode = mode;
            st.upsideDown = upside;
            st.mini = mini;
            if (appeared) {
                // second player spawning in from a dual portal
                auto pos = toLocal(p);
                auto c = MODE_COLORS[mode];
                ringBurst(pos, c, 10.f, 130.f, .45f);
                implode(pos, lighten(c), 10, 70.f, .22f);
                if (auto flash = spawn(Tex::Glow, c, pos, 30.f, 30.f, 110.f, 110.f, .3f)) flash->alpha *= .9f;
            }
            continue;
        }

        if (canFx) {
            if (mode != st.mode) modeTransition(p, st, st.mode, mode);
            else {
                if (upside != st.upsideDown) gravityTransition(p, upside);
                if (mini != st.mini) sizeTransition(p, mini);
            }
        }
        st.mode = mode;
        st.upsideDown = upside;
        st.mini = mini;
    }
}

void AnimFX::modeTransition(PlayerObject* player, PlayerState& st, int oldMode, int newMode) {
    auto pos = toLocal(player);
    auto c = MODE_COLORS[newMode];
    auto old = MODE_COLORS[std::clamp(oldMode, 0, 7)];

    // shockwaves
    ringBurst(pos, c, 12.f, 170.f, .5f);
    ringBurst(pos, lighten(c), 8.f, 100.f, .32f, 1.f, frand(0.f, 360.f), 230.f, Tex::DashRing);
    // core flash
    if (auto flash = spawn(Tex::Glow, c, pos, 40.f, 40.f, 130.f, 130.f, .3f)) flash->alpha *= .95f;
    // the old form gets sucked in, the new one explodes out
    implode(pos, old, 10, 80.f, .22f);
    sparkBurst(pos, c, 20, 160.f, 460.f, 0.f, 360.f, .55f);

    // flare the portal we came through (no-op if its own hook already did)
    if (auto portal = player->m_lastActivatedPortal) {
        auto pp = toLocal(portal);
        float dx = pp.x - pos.x, dy = pp.y - pos.y;
        if (dx * dx + dy * dy < 250.f * 250.f) onObjectActivated(portal);
    }

    if (m_cfg.pop && player->m_mainLayer) {
        if (st.popStart < 0.f) st.popBase = player->m_mainLayer->getScale();
        st.popStart = m_realTime;
    }

    if (m_cfg.flash && m_flash) {
        m_flash->setColor(c);
        m_flashAlpha = 1.f;
    }

    if (m_cfg.modeName) {
        auto label = CCLabelBMFont::create(MODE_NAMES[newMode], "bigFont.fnt");
        label->setColor(lighten(c, .2f));
        m_burstLayer->addChild(label);
        Particle p;
        p.node = label;
        p.rgba = label;
        p.pos = CCPoint(pos.x, pos.y + 48.f);
        p.vel = CCPoint(0.f, 60.f);
        p.life = .9f;
        p.sx0 = p.sy0 = .2f;
        p.sx1 = p.sy1 = .65f;
        p.drag = .2f;
        p.fadePow = 3.f;
        label->setPosition(p.pos);
        label->setScale(p.sx0);
        m_particles.push_back(p);
    }
}

void AnimFX::gravityTransition(PlayerObject* player, bool upsideDown) {
    auto pos = toLocal(player);
    auto c = upsideDown ? GRAVITY_INVERSE : GRAVITY_NORMAL;
    ringBurst(pos, c, 10.f, 110.f, .35f);
    // spray opposite to the new direction of gravity
    sparkBurst(pos, c, 10, 150.f, 380.f, upsideDown ? -90.f : 90.f, 60.f, .4f);
}

void AnimFX::sizeTransition(PlayerObject* player, bool mini) {
    auto pos = toLocal(player);
    if (mini) {
        ringBurst(pos, SIZE_MINI, 140.f, 12.f, .3f);
        implode(pos, SIZE_MINI, 10, 70.f, .2f);
    }
    else {
        ringBurst(pos, SIZE_BIG, 12.f, 150.f, .4f);
        sparkBurst(pos, SIZE_BIG, 12, 140.f, 360.f, 0.f, 360.f, .4f);
    }
}
