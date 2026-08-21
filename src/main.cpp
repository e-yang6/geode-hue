#include <Geode/Geode.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

// HSV/RGB conversion helpers
struct HsvColor {
    float h, s, v;
};

static HsvColor rgbToHsv(const ccColor3B& c) {
    float r = c.r / 255.0f;
    float g = c.g / 255.0f;
    float b = c.b / 255.0f;

    float maxC = std::max({r, g, b});
    float minC = std::min({r, g, b});
    float delta = maxC - minC;

    HsvColor hsv;
    hsv.v = maxC;
    hsv.s = (maxC == 0.0f) ? 0.0f : (delta / maxC);

    if (delta == 0.0f) {
        hsv.h = 0.0f;
    } else if (maxC == r) {
        hsv.h = 60.0f * fmodf((g - b) / delta + 6.0f, 6.0f);
    } else if (maxC == g) {
        hsv.h = 60.0f * ((b - r) / delta + 2.0f);
    } else {
        hsv.h = 60.0f * ((r - g) / delta + 4.0f);
    }

    return hsv;
}

static ccColor3B hsvToRgb(const HsvColor& hsv) {
    float c = hsv.v * hsv.s;
    float x = c * (1.0f - fabsf(fmodf(hsv.h / 60.0f, 2.0f) - 1.0f));
    float m = hsv.v - c;

    float r, g, b;
    if (hsv.h < 60.0f)       { r = c; g = x; b = 0; }
    else if (hsv.h < 120.0f) { r = x; g = c; b = 0; }
    else if (hsv.h < 180.0f) { r = 0; g = c; b = x; }
    else if (hsv.h < 240.0f) { r = 0; g = x; b = c; }
    else if (hsv.h < 300.0f) { r = x; g = 0; b = c; }
    else                     { r = c; g = 0; b = x; }

    return {
        static_cast<GLubyte>((r + m) * 255.0f),
        static_cast<GLubyte>((g + m) * 255.0f),
        static_cast<GLubyte>((b + m) * 255.0f)
    };
}

static ccColor3B shiftHue(const ccColor3B& color, float shiftDegrees) {
    if (shiftDegrees == 0.0f) return color;
    // Don't shift pure black or pure white
    if (color.r == 0 && color.g == 0 && color.b == 0) return color;
    if (color.r == 255 && color.g == 255 && color.b == 255) return color;

    HsvColor hsv = rgbToHsv(color);
    if (hsv.s < 0.01f) return color; // skip near-gray colors

    hsv.h = fmodf(hsv.h + shiftDegrees, 360.0f);
    if (hsv.h < 0.0f) hsv.h += 360.0f;

    return hsvToRgb(hsv);
}

static bool isPortalOrSpeedPortal(GameObject* obj) {
    auto type = obj->m_objectType;
    switch (type) {
        case GameObjectType::InverseGravityPortal:
        case GameObjectType::NormalGravityPortal:
        case GameObjectType::ShipPortal:
        case GameObjectType::CubePortal:
        case GameObjectType::InverseMirrorPortal:
        case GameObjectType::NormalMirrorPortal:
        case GameObjectType::BallPortal:
        case GameObjectType::RegularSizePortal:
        case GameObjectType::MiniSizePortal:
        case GameObjectType::UfoPortal:
        case GameObjectType::WavePortal:
        case GameObjectType::RobotPortal:
        case GameObjectType::TeleportPortal:
        case GameObjectType::DualPortal:
        case GameObjectType::SoloPortal:
        case GameObjectType::SpiderPortal:
        case GameObjectType::SwingPortal:
        case GameObjectType::GravityTogglePortal:
            return true;
        default:
            break;
    }

    // Speed portals by object ID
    int objID = obj->m_objectID;
    if (objID == 200 || objID == 201 || objID == 202 || objID == 203 || objID == 1334) {
        return true;
    }

    return false;
}

static float getShift() {
    return static_cast<float>(Mod::get()->getSettingValue<double>("hue-shift"));
}

static bool isEnabled() {
    return Mod::get()->getSettingValue<bool>("enabled");
}

class $modify(HuePauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto sprite = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
        sprite->setScale(0.65f);

        auto btn = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(HuePauseLayer::onHueSettings)
        );

        auto menu = CCMenu::create();
        menu->addChild(btn);
        menu->setPosition({42.0f, 42.0f});
        this->addChild(menu, 10);
    }

    void onHueSettings(CCObject*) {
        geode::openSettingsPopup(Mod::get());
    }
};

class $modify(HueGameObject, GameObject) {
    void setObjectColor(cocos2d::ccColor3B const& color) {
        if (!isEnabled() || isPortalOrSpeedPortal(this)) {
            GameObject::setObjectColor(color);
            return;
        }

        ccColor3B shifted = shiftHue(color, getShift());
        GameObject::setObjectColor(shifted);
    }

    void setChildColor(cocos2d::ccColor3B const& color) {
        if (!isEnabled() || isPortalOrSpeedPortal(this)) {
            GameObject::setChildColor(color);
            return;
        }

        ccColor3B shifted = shiftHue(color, getShift());
        GameObject::setChildColor(shifted);
    }
};

class $modify(HueBaseGameLayer, GJBaseGameLayer) {
    void updateColor(
        cocos2d::ccColor3B& color,
        float fadeTime,
        int colorID,
        bool blending,
        float opacity,
        cocos2d::ccHSVValue& copyHSV,
        int colorIDToCopy,
        bool copyOpacity,
        EffectGameObject* callerObject,
        int unk1,
        int unk2
    ) {
        if (!isEnabled()) {
            GJBaseGameLayer::updateColor(color, fadeTime, colorID, blending, opacity, copyHSV, colorIDToCopy, copyOpacity, callerObject, unk1, unk2);
            return;
        }

        float shift = getShift();

        // Special color channel IDs:
        // 1005 = Player Color 1, 1006 = Player Color 2 — always skip
        // 1000 = BG, 1007 = LBG — skip if shift-bg is off
        // 1001 = G1 (ground), 1009 = G2 (ground 2) — skip if shift-ground is off

        bool isPlayerColor = (colorID == 1005 || colorID == 1006);
        bool isBG = (colorID == 1000 || colorID == 1007);
        bool isGround = (colorID == 1001 || colorID == 1009);

        if (isPlayerColor) {
            GJBaseGameLayer::updateColor(color, fadeTime, colorID, blending, opacity, copyHSV, colorIDToCopy, copyOpacity, callerObject, unk1, unk2);
            return;
        }

        if (isBG && !Mod::get()->getSettingValue<bool>("shift-bg")) {
            GJBaseGameLayer::updateColor(color, fadeTime, colorID, blending, opacity, copyHSV, colorIDToCopy, copyOpacity, callerObject, unk1, unk2);
            return;
        }

        if (isGround && !Mod::get()->getSettingValue<bool>("shift-ground")) {
            GJBaseGameLayer::updateColor(color, fadeTime, colorID, blending, opacity, copyHSV, colorIDToCopy, copyOpacity, callerObject, unk1, unk2);
            return;
        }

        ccColor3B shifted = shiftHue(color, shift);
        GJBaseGameLayer::updateColor(shifted, fadeTime, colorID, blending, opacity, copyHSV, colorIDToCopy, copyOpacity, callerObject, unk1, unk2);
    }
};
