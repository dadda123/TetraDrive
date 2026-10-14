#include "PresetManager.h"

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& apvts) : apvts_(apvts) {
    for (int i = 0; i < td::kNumParams; ++i) snapshot_[static_cast<std::size_t>(i)] = td::kParams[static_cast<std::size_t>(i)].def;
}

juce::File PresetManager::presetDirectory() {
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
#if JUCE_MAC
                   .getChildFile("Application Support")
#endif
                   .getChildFile("TetraDrive")
                   .getChildFile("Presets");
    if (!dir.isDirectory()) dir.createDirectory();
    return dir;
}

juce::File PresetManager::fileFor(const juce::String& name) const {
    return presetDirectory().getChildFile(juce::File::createLegalFileName(name.trim()) + kExtension);
}

juce::StringArray PresetManager::names() const {
    juce::StringArray user;
    for (const auto& f : presetDirectory().findChildFiles(juce::File::findFiles, false, juce::String("*") + kExtension))
        user.add(f.getFileNameWithoutExtension());
    user.sortNatural();
    user.removeString(kDefaultName, true);
    user.insert(0, kDefaultName);
    return user;
}

bool PresetManager::isUserPreset(const juce::String& name) const {
    return !name.equalsIgnoreCase(kDefaultName) && fileFor(name).existsAsFile();
}

bool PresetManager::exists(const juce::String& name) const {
    return name.equalsIgnoreCase(kDefaultName) || fileFor(name).existsAsFile();
}

std::array<float, td::kNumParams> PresetManager::currentReal() const {
    std::array<float, td::kNumParams> v{};
    for (int i = 0; i < td::kNumParams; ++i) {
        auto* p = apvts_.getParameter(td::kParams[static_cast<std::size_t>(i)].id);
        v[static_cast<std::size_t>(i)] = p->convertFrom0to1(p->getValue());
    }
    return v;
}

void PresetManager::applyReal(const std::array<float, td::kNumParams>& values) {
    for (int i = 0; i < td::kNumParams; ++i) {
        if (!includedInPresets(i)) continue;
        auto* p = apvts_.getParameter(td::kParams[static_cast<std::size_t>(i)].id);
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(values[static_cast<std::size_t>(i)]));
        p->endChangeGesture();
    }
    snapshot_ = currentReal();
}

bool PresetManager::load(const juce::String& name) {
    std::array<float, td::kNumParams> values{};
    for (int i = 0; i < td::kNumParams; ++i) values[static_cast<std::size_t>(i)] = td::kParams[static_cast<std::size_t>(i)].def;

    if (!name.equalsIgnoreCase(kDefaultName)) {
        auto xml = juce::XmlDocument::parse(fileFor(name));
        if (xml == nullptr || !xml->hasTagName("TetraDrivePreset")) return false;
        // missing values fall back to defaults
        for (auto* e : xml->getChildWithTagNameIterator("Param"))
            for (int i = 0; i < td::kNumParams; ++i)
                if (e->getStringAttribute("id") == td::kParams[static_cast<std::size_t>(i)].id)
                    values[static_cast<std::size_t>(i)] = static_cast<float>(e->getDoubleAttribute("value", values[static_cast<std::size_t>(i)]));
    }
    applyReal(values);
    current_ = name.equalsIgnoreCase(kDefaultName) ? juce::String(kDefaultName) : name;
    return true;
}

bool PresetManager::save(const juce::String& name) {
    const auto clean = name.trim();
    if (clean.isEmpty() || clean.equalsIgnoreCase(kDefaultName)) return false;
    juce::XmlElement xml("TetraDrivePreset");
    xml.setAttribute("formatVersion", kFormatVersion);
    xml.setAttribute("pluginVersion", JucePlugin_VersionString);
    xml.setAttribute("name", clean);
    const auto values = currentReal();
    for (int i = 0; i < td::kNumParams; ++i) {
        if (!includedInPresets(i)) continue;
        auto* e = xml.createNewChildElement("Param");
        e->setAttribute("id", td::kParams[static_cast<std::size_t>(i)].id);
        e->setAttribute("value", values[static_cast<std::size_t>(i)]);
    }
    if (!xml.writeTo(fileFor(clean))) return false;
    current_ = clean;
    snapshot_ = values;
    return true;
}

bool PresetManager::remove(const juce::String& name) {
    if (!isUserPreset(name)) return false;
    if (!fileFor(name).deleteFile()) return false;
    if (current_ == name) {
        current_ = kDefaultName;
        for (int i = 0; i < td::kNumParams; ++i) snapshot_[static_cast<std::size_t>(i)] = td::kParams[static_cast<std::size_t>(i)].def;
    }
    return true;
}

void PresetManager::loadNext(int direction) {
    const auto list = names();
    int idx = list.indexOf(current_, true);
    if (idx < 0) idx = 0;
    idx = (idx + direction + list.size()) % list.size();
    load(list[idx]);
}

void PresetManager::restoreCurrent(const juce::String& name) {
    current_ = exists(name) ? name : juce::String(kDefaultName);
    snapshot_ = currentReal();
}

bool PresetManager::isModified() const {
    const auto now = currentReal();
    for (int i = 0; i < td::kNumParams; ++i) {
        if (!includedInPresets(i)) continue;
        const auto& s = td::kParams[static_cast<std::size_t>(i)];
        const float tol = (s.max - s.min) * 1e-4f;
        if (std::abs(now[static_cast<std::size_t>(i)] - snapshot_[static_cast<std::size_t>(i)]) > tol) return true;
    }
    return false;
}
