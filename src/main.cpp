#include <Geode/Geode.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/Popup.hpp>

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
    if (color.r == 0 && color.g == 0 && color.b == 0) return color;
    if (color.r == 255 && color.g == 255 && color.b == 255) return color;

    HsvColor hsv = rgbToHsv(color);
    if (hsv.s < 0.01f) return color;

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

// State for real-time color preview
static bool s_bypassHook = false;
static bool s_inUpdateColor = false;
static std::unordered_map<GameObject*, ccColor3B> s_origObjColors;
static std::unordered_map<GameObject*, ccColor3B> s_origChildColors;

struct ChannelData {
    ccColor3B color;
    float fadeTime;
    int colorID;
    bool blending;
    float opacity;
    ccHSVValue copyHSV;
    int colorIDToCopy;
    bool copyOpacity;
};
static std::unordered_map<int, ChannelData> s_origChannels;

static void refreshAllColors() {
    auto gl = GJBaseGameLayer::get();
    if (!gl) return;

    float shift = getShift();
    bool enabled = isEnabled();
    bool shiftBg = Mod::get()->getSettingValue<bool>("shift-bg");
    bool shiftGround = Mod::get()->getSettingValue<bool>("shift-ground");

    // Refresh object colors from stored originals
    s_bypassHook = true;
    if (gl->m_objects) {
        for (auto obj : CCArrayExt<GameObject*>(gl->m_objects)) {
            bool skip = !enabled || isPortalOrSpeedPortal(obj);

            auto it = s_origObjColors.find(obj);
            if (it != s_origObjColors.end()) {
                ccColor3B c = skip ? it->second : shiftHue(it->second, shift);
                obj->setObjectColor(c);
            }

            auto it2 = s_origChildColors.find(obj);
            if (it2 != s_origChildColors.end()) {
                ccColor3B c = skip ? it2->second : shiftHue(it2->second, shift);
                obj->setChildColor(c);
            }
        }
    }

    // Refresh color channels (BG, ground, etc.)
    for (auto& [id, data] : s_origChannels) {
        ccColor3B color = data.color;

        if (enabled) {
            bool isPlayerColor = (id == 1005 || id == 1006);
            bool isBG = (id == 1000 || id == 1007);
            bool isGround = (id == 1001 || id == 1009);

            if (!isPlayerColor && !(isBG && !shiftBg) && !(isGround && !shiftGround)) {
                color = shiftHue(color, shift);
            }
        }

        ccHSVValue hsv = data.copyHSV;
        gl->updateColor(color, data.fadeTime, data.colorID, data.blending,
            data.opacity, hsv, data.colorIDToCopy, data.copyOpacity, nullptr, 0, 0);
    }
    s_bypassHook = false;
}

// Custom settings popup with real-time slider preview
class HueSettingsPopup : public geode::Popup {
protected:
    Slider* m_hueSlider = nullptr;
    CCLabelBMFont* m_valueLabel = nullptr;

    void onSliderChanged(CCObject*) {
        float value = m_hueSlider->getValue() * 360.0f;
        value = std::clamp(value, 0.0f, 360.0f);

        Mod::get()->setSettingValue<double>("hue-shift", static_cast<double>(value));
        m_valueLabel->setString(fmt::format("{:.0f}", value).c_str());

        refreshAllColors();
    }

public:
    static HueSettingsPopup* create() {
        auto ret = new HueSettingsPopup();
        if (ret && ret->initPopup()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool initPopup() {
        if (!Popup::init(300, 120)) return false;

        this->setTitle("Hue Shift");

        auto contentSize = m_mainLayer->getContentSize();
        float centerX = contentSize.width / 2;
        float centerY = contentSize.height / 2;

        float currentValue = getShift();

        m_valueLabel = CCLabelBMFont::create(
            fmt::format("{:.0f}", currentValue).c_str(),
            "bigFont.fnt"
        );
        m_valueLabel->setScale(0.5f);
        m_valueLabel->setPosition({centerX, centerY + 15});
        m_mainLayer->addChild(m_valueLabel);

        m_hueSlider = Slider::create(this, menu_selector(HueSettingsPopup::onSliderChanged), 0.8f);
        m_hueSlider->setValue(currentValue / 360.0f);
        m_hueSlider->setPosition({centerX, centerY - 15});
        m_mainLayer->addChild(m_hueSlider);

        auto minLabel = CCLabelBMFont::create("0", "goldFont.fnt");
        minLabel->setScale(0.4f);
        minLabel->setPosition({centerX - 130, centerY - 15});
        m_mainLayer->addChild(minLabel);

        auto maxLabel = CCLabelBMFont::create("360", "goldFont.fnt");
        maxLabel->setScale(0.4f);
        maxLabel->setPosition({centerX + 130, centerY - 15});
        m_mainLayer->addChild(maxLabel);

        return true;
    }
};

class $modify(HuePauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

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
        HueSettingsPopup::create()->show();
    }
};

class $modify(HueGameObject, GameObject) {
    void setObjectColor(cocos2d::ccColor3B const& color) {
        if (s_bypassHook) {
            GameObject::setObjectColor(color);
            return;
        }

        // If called from updateColor, the color is already shifted — pass through
        if (s_inUpdateColor) {
            GameObject::setObjectColor(color);
            return;
        }

        s_origObjColors[this] = color;

        if (!isEnabled() || isPortalOrSpeedPortal(this)) {
            GameObject::setObjectColor(color);
            return;
        }

        ccColor3B shifted = shiftHue(color, getShift());
        GameObject::setObjectColor(shifted);
    }

    void setChildColor(cocos2d::ccColor3B const& color) {
        if (s_bypassHook) {
            GameObject::setChildColor(color);
            return;
        }

        if (s_inUpdateColor) {
            GameObject::setChildColor(color);
            return;
        }

        s_origChildColors[this] = color;

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
        if (s_bypassHook) {
            GJBaseGameLayer::updateColor(color, fadeTime, colorID, blending, opacity, copyHSV, colorIDToCopy, copyOpacity, callerObject, unk1, unk2);
            return;
        }

        s_origChannels[colorID] = {color, fadeTime, colorID, blending, opacity, copyHSV, colorIDToCopy, copyOpacity};

        bool shouldShift = isEnabled();
        if (shouldShift) {
            bool isPlayerColor = (colorID == 1005 || colorID == 1006);
            bool isBG = (colorID == 1000 || colorID == 1007);
            bool isGround = (colorID == 1001 || colorID == 1009);

            if (isPlayerColor) shouldShift = false;
            else if (isBG && !Mod::get()->getSettingValue<bool>("shift-bg")) shouldShift = false;
            else if (isGround && !Mod::get()->getSettingValue<bool>("shift-ground")) shouldShift = false;
        }

        ccColor3B finalColor = shouldShift ? shiftHue(color, getShift()) : color;
        s_inUpdateColor = true;
        GJBaseGameLayer::updateColor(finalColor, fadeTime, colorID, blending, opacity, copyHSV, colorIDToCopy, copyOpacity, callerObject, unk1, unk2);
        s_inUpdateColor = false;
    }
};
