#include "PluginProcessor.h"

#ifndef TD_HEADLESS_TEST
#include "PluginEditor.h"
#endif

namespace {

// Log mapping for Hz parameters.
juce::NormalisableRange<float> hzRange(float lo, float hi) {
    return {lo, hi,
            [](float a, float b, float v) { return a * std::pow(b / a, v); },
            [](float a, float b, float x) { return std::log(x / a) / std::log(b / a); },
            [](float a, float b, float x) { return juce::jlimit(a, b, x); }};
}

bool isDiscrete(int index) { return index == td::kShape || index == td::kSolo || index == td::kNumParams; }

}  // namespace

juce::AudioProcessorValueTreeState::ParameterLayout TetraDriveProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < td::kNumParams; ++i) {
        const auto& s = td::kParams[static_cast<std::size_t>(i)];
        const juce::ParameterID id{s.id, 1};
        if (s.unit == td::Unit::Choice) {
            juce::StringArray names;
            if (i == td::kShape) for (auto* n : td::kShapeNames) names.add(n);
            else for (auto* n : td::kSoloNames) names.add(n);
            layout.add(std::make_unique<juce::AudioParameterChoice>(id, s.name, names, static_cast<int>(s.def)));
            continue;
        }
        auto range = s.logScale ? hzRange(s.min, s.max) : juce::NormalisableRange<float>(s.min, s.max, s.step);
        auto attrs = juce::AudioParameterFloatAttributes()
                         .withStringFromValueFunction([i](float v, int) { return juce::String(td::formatValue(i, v)); })
                         .withValueFromStringFunction([i](const juce::String& t) { return td::parseValue(i, t.toStdString()); });
        layout.add(std::make_unique<juce::AudioParameterFloat>(id, s.name, range, s.def, attrs));
    }
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass", 1}, "Bypass", false));
    return layout;
}

TetraDriveProcessor::TetraDriveProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "TetraDrive", createLayout()),
      presets(apvts) {
    for (int i = 0; i < td::kNumParams; ++i) {
        params_[static_cast<std::size_t>(i)] = apvts.getParameter(td::kParams[static_cast<std::size_t>(i)].id);
        jassert(params_[static_cast<std::size_t>(i)]->getParameterIndex() == i);
    }
    bypass_ = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("bypass"));
    jassert(bypass_ != nullptr && bypass_->getParameterIndex() == kBypassIndex);
    for (int e = 0; e <= td::kBands; ++e) edgeTargets_[static_cast<std::size_t>(e)] = td::kParams[static_cast<std::size_t>(td::edgeParam(e))].def;
    syncAllParams(true);
}

juce::String TetraDriveProcessor::versionString() { return JucePlugin_VersionString; }

bool TetraDriveProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void TetraDriveProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_.store(sampleRate);
    engine_.prepare(sampleRate, juce::jlimit(1, td::kMaxChannels, getTotalNumInputChannels()));
    engine_.reserve(juce::jmax(samplesPerBlock, 64));
    analyzer.prepare(sampleRate);
    events_.clear();
    syncAllParams(true);
}

void TetraDriveProcessor::addParameterPoint(juce::AudioProcessorParameter& parameter, int sampleOffset, float value) noexcept {
    const int index = parameter.getParameterIndex();
    if (index >= 0 && index <= kBypassIndex) events_.add(sampleOffset, index, value);
}

void TetraDriveProcessor::pushEdges(int rampSamples) {
    engine_.setEdgeTargets(edgeTargets_.data(), rampSamples);
}

void TetraDriveProcessor::applyParam(int index, float normalised, int rampSamples) {
    applied_[static_cast<std::size_t>(index)] = normalised;
    if (index == kBypassIndex) {
        bypassed_ = normalised >= 0.5f;
        engine_.setRouteTarget(soloBand_, bypassed_ || forcedBypass_);
        return;
    }
    const float real = params_[static_cast<std::size_t>(index)]->convertFrom0to1(normalised);
    switch (index) {
        case td::kLowCut: case td::kXover1: case td::kXover2: case td::kXover3: case td::kHighCut:
            edgeTargets_[static_cast<std::size_t>(index - td::kLowCut)] = real;
            pushEdges(rampSamples);
            break;
        case td::kDrive1: case td::kDrive2: case td::kDrive3: case td::kDrive4:
            engine_.setBandDriveTarget(index - td::kDrive1, real, rampSamples);
            break;
        case td::kLevel1: case td::kLevel2: case td::kLevel3: case td::kLevel4:
            engine_.setBandGainTarget(index - td::kLevel1, real, rampSamples);
            break;
        case td::kInput: engine_.setInputGainTarget(real, rampSamples); break;
        case td::kOutput: engine_.setOutputGainTarget(real, rampSamples); break;
        case td::kShape: engine_.setShapeTarget(static_cast<int>(std::lround(real))); break;
        case td::kSolo:
            soloBand_ = static_cast<int>(std::lround(real)) - 1;
            engine_.setRouteTarget(soloBand_, bypassed_ || forcedBypass_);
            break;
        default: break;
    }
}

void TetraDriveProcessor::syncAllParams(bool snap) {
    for (int i = 0; i < td::kNumParams; ++i) applyParam(i, params_[static_cast<std::size_t>(i)]->getValue(), -1);
    applyParam(kBypassIndex, (bypass_->get() ? 1.0f : 0.0f), -1);
    if (snap) engine_.snapToTargets();
}

void TetraDriveProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    processInternal(buffer, false);
}

void TetraDriveProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    processInternal(buffer, true);
}

void TetraDriveProcessor::processInternal(juce::AudioBuffer<float>& buffer, bool forceBypass) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = juce::jmin(engine_.channels(), buffer.getNumChannels());
    if (n <= 0 || channels <= 0) { events_.clear(); return; }

    analyzer.push(buffer.getArrayOfReadPointers(), channels, n);

    // host bypass without a parameter change
    if (forceBypass != forcedBypass_) {
        forcedBypass_ = forceBypass;
        engine_.setRouteTarget(soloBand_, bypassed_ || forcedBypass_);
    }

    events_.sort();
    hasEvents_.fill(false);
    for (int k = 0; k < events_.size(); ++k) hasEvents_[static_cast<std::size_t>(events_[k].param)] = true;

    // block-start values for parameters without points (GUI and AU)
    for (int i = 0; i <= kBypassIndex; ++i) {
        if (hasEvents_[static_cast<std::size_t>(i)]) continue;
        const float v = (i == kBypassIndex) ? (bypass_->get() ? 1.0f : 0.0f) : params_[static_cast<std::size_t>(i)]->getValue();
        if (v != applied_[static_cast<std::size_t>(i)]) applyParam(i, v, -1);
    }

    // continuous parameters ramp from block start to their first point
    for (int k = 0; k < events_.size(); ++k) {
        const auto& e = events_[k];
        if (isDiscrete(e.param)) continue;
        bool first = true;
        for (int j = 0; j < k && first; ++j) first = events_[j].param != e.param;
        if (first) applyParam(e.param, e.normalised, juce::jlimit(0, n, e.offset));
    }

    float* const* io = buffer.getArrayOfWritePointers();
    float* seg[td::kMaxChannels] = {};
    int pos = 0, k = 0;
    while (pos < n) {
        const int next = (k < events_.size()) ? juce::jlimit(pos, n, events_[k].offset) : n;
        if (next > pos) {
            for (int c = 0; c < channels; ++c) seg[c] = io[c] + pos;
            engine_.process(seg, seg, next - pos);
            pos = next;
        }
        // at this offset: switch discrete values, start ramps toward the next points
        while (k < events_.size() && juce::jmin(events_[k].offset, n) <= pos) {
            const auto& e = events_[k];
            if (isDiscrete(e.param)) {
                applyParam(e.param, e.normalised, -1);
            } else {
                for (int j = k + 1; j < events_.size(); ++j)
                    if (events_[j].param == e.param) {
                        applyParam(e.param, events_[j].normalised, juce::jmax(0, events_[j].offset - e.offset));
                        break;
                    }
            }
            ++k;
        }
        if (k >= events_.size() && pos >= n) break;
    }
    events_.clear();

    for (int c = channels; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, n);
}

void TetraDriveProcessor::setRealValue(int index, float real) {
    auto& p = param(index);
    p.beginChangeGesture();
    p.setValueNotifyingHost(p.convertTo0to1(real));
    p.endChangeGesture();
}

juce::AudioProcessorEditor* TetraDriveProcessor::createEditor() {
#ifdef TD_HEADLESS_TEST
    return nullptr;
#else
    return new TetraDriveEditor(*this);
#endif
}

void TetraDriveProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    state.setProperty("version", versionString(), nullptr);
    state.setProperty("presetName", presets.currentName(), nullptr);
    state.removeChild(state.getChildWithName("UI"), nullptr);
    juce::ValueTree uiTree("UI");
    uiTree.setProperty("width", ui.width.load(), nullptr);
    uiTree.setProperty("height", ui.height.load(), nullptr);
    uiTree.setProperty("fftOrder", ui.fftOrder.load(), nullptr);
    uiTree.setProperty("analyzerOn", ui.analyzerOn.load(), nullptr);
    uiTree.setProperty("helpBar", ui.helpBar.load(), nullptr);
    state.appendChild(uiTree, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void TetraDriveProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr) return;
    auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.hasType(apvts.state.getType())) return;

    if (auto uiTree = tree.getChildWithName("UI"); uiTree.isValid()) {
        ui.width = static_cast<int>(uiTree.getProperty("width", ui.width.load()));
        ui.height = static_cast<int>(uiTree.getProperty("height", ui.height.load()));
        ui.fftOrder = juce::jlimit(12, 14, static_cast<int>(uiTree.getProperty("fftOrder", ui.fftOrder.load())));
        ui.analyzerOn = static_cast<bool>(uiTree.getProperty("analyzerOn", ui.analyzerOn.load()));
        ui.helpBar = static_cast<bool>(uiTree.getProperty("helpBar", ui.helpBar.load()));
        tree.removeChild(uiTree, nullptr);
    }
    const auto presetName = tree.getProperty("presetName", PresetManager::kDefaultName).toString();
    apvts.replaceState(tree);
    presets.restoreCurrent(presetName);
}

#ifndef TD_HEADLESS_TEST
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TetraDriveProcessor(); }
#endif
