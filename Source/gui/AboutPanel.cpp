#include "AboutPanel.h"

#include <BinaryData.h>

#include "../PluginProcessor.h"
#include "TetraLookAndFeel.h"

namespace tdui {

namespace {

juce::String resourceText(const char* name) {
    int size = 0;
    const auto* data = TetraDriveBinary::getNamedResource(name, size);
    return data != nullptr ? juce::String::fromUTF8(data, size) : juce::String();
}

juce::String versionLine() {
    juce::String v = "Version " + TetraDriveProcessor::versionString();
    juce::StringArray extra;
    if (juce::String(TD_GIT_HASH).isNotEmpty()) extra.add(TD_GIT_HASH);
    if (juce::String(TD_BUILD_DATE).isNotEmpty()) extra.add(TD_BUILD_DATE);
    if (!extra.isEmpty()) v << " (" << extra.joinIntoString(", ") << ")";
    return v;
}

struct TextLink {
    juce::Range<int> range;
    juce::URL url;
};

// Appends text, turning every http(s) URL into an underlined link.
void appendWithLinks(juce::AttributedString& t, std::vector<TextLink>& links, const juce::String& text,
                     const juce::Font& font, juce::Colour colour) {
    juce::String rest = text;
    while (rest.isNotEmpty()) {
        const int a = rest.indexOf("https://"), b = rest.indexOf("http://");
        const int start = (a < 0) ? b : (b < 0 ? a : juce::jmin(a, b));
        if (start < 0) { t.append(rest, font, colour); break; }
        if (start > 0) t.append(rest.substring(0, start), font, colour);
        int end = start;
        while (end < rest.length() && !juce::CharacterFunctions::isWhitespace(rest[end])) ++end;
        while (end > start && juce::String(".,;:)").containsChar(rest[end - 1])) --end;
        const auto url = rest.substring(start, end);
        const int from = t.getText().length();
        t.append(url, juce::Font(font).withStyle(juce::Font::underlined), colours::bands[0]);
        links.push_back({{from, from + url.length()}, juce::URL(url)});
        rest = rest.substring(end);
    }
}

}  // namespace

// Scrollable block of styled text with clickable links.
class AboutPanel::TextPage : public juce::Component {
public:
    void setText(const juce::AttributedString& s, int width, std::vector<TextLink> links) {
        text_ = s;
        layout_.createLayout(text_, static_cast<float>(width));
        setSize(width, static_cast<int>(std::ceil(layout_.getHeight())) + 8);
        // hit areas: every glyph run that falls inside a link's character range
        hits_.clear();
        for (const auto& line : layout_)
            for (const auto* run : line.runs)
                for (const auto& link : links)
                    if (link.range.intersects(run->stringRange)) {
                        const auto x = run->getRunBoundsX() + line.lineOrigin.x;
                        hits_.push_back({juce::Rectangle<float>(x.getStart(), line.lineOrigin.y - line.ascent,
                                                                x.getLength(), line.ascent + line.descent),
                                         link.url});
                    }
        repaint();
    }
    void paint(juce::Graphics& g) override { layout_.draw(g, getLocalBounds().toFloat()); }

    void mouseMove(const juce::MouseEvent& e) override {
        setMouseCursor(linkAt(e.position) != nullptr ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (const auto* url = linkAt(e.position); url != nullptr && e.mouseWasClicked()) url->launchInDefaultBrowser();
    }

    int numLinkAreas() const { return static_cast<int>(hits_.size()); }

private:
    struct Hit { juce::Rectangle<float> area; juce::URL url; };

    const juce::URL* linkAt(juce::Point<float> p) const {
        for (const auto& h : hits_)
            if (h.area.expanded(1.0f, 2.0f).contains(p)) return &h.url;
        return nullptr;
    }

    juce::AttributedString text_;
    juce::TextLayout layout_;
    std::vector<Hit> hits_;
};

AboutPanel::AboutPanel() {
    about_ = juce::JSON::parse(resourceText("About_json"));
    changelog_ = resourceText("CHANGELOG_md");

    setWantsKeyboardFocus(true);
    for (auto* b : {&aboutTab_, &notesTab_}) {
        b->setRadioGroupId(4711);
        b->setClickingTogglesState(true);
        addAndMakeVisible(b);
    }
    aboutTab_.onClick = [this] { showTab(0); };
    notesTab_.onClick = [this] { showTab(1); };

    auto normal = TetraLookAndFeel::icon("close_svg", colours::textSecondary);
    auto over = TetraLookAndFeel::icon("close_svg", colours::textPrimary);
    close_.setImages(normal.get(), over.get(), over.get());
    close_.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    close_.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(close_);

    const auto link = about_["link"];
    link_.setButtonText(link["label"].toString());
    link_.setURL(juce::URL(link["url"].toString()));
    link_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(link_);

    page_ = std::make_unique<TextPage>();
    viewport_.setViewedComponent(page_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(6);
    addAndMakeVisible(viewport_);
    aboutTab_.setToggleState(true, juce::dontSendNotification);
}

AboutPanel::~AboutPanel() { viewport_.setViewedComponent(nullptr, false); }

juce::Rectangle<int> AboutPanel::cardBounds() const {
    const float s = scaleOf(const_cast<AboutPanel&>(*this));
    const int w = juce::jmin(static_cast<int>(600.0f * s), getWidth() - 32);
    const int h = juce::jmin(static_cast<int>(560.0f * s), getHeight() - 32);
    return getLocalBounds().withSizeKeepingCentre(w, h);
}

void AboutPanel::paint(juce::Graphics& g) {
    const float s = scaleOf(*this);
    g.fillAll(colours::background.withAlpha(0.75f));
    const auto card = cardBounds().toFloat();
    g.setColour(colours::panel);
    g.fillRoundedRectangle(card, 8.0f * s);
    g.setColour(colours::outline);
    g.drawRoundedRectangle(card.reduced(0.5f), 8.0f * s, 1.0f);

    auto head = card.reduced(24.0f * s, 20.0f * s);
    g.setColour(colours::textPrimary);
    g.setFont(TetraLookAndFeel::font(26.0f * s, Weight::SemiBold));
    g.drawText(about_["name"].toString(), head.removeFromTop(32.0f * s), juce::Justification::centredLeft);
    g.setColour(colours::textSecondary);
    g.setFont(TetraLookAndFeel::font(14.0f * s));
    g.drawText(about_["tagline"].toString(), head.removeFromTop(20.0f * s), juce::Justification::centredLeft);
    g.setFont(TetraLookAndFeel::font(12.0f * s, Weight::Medium));
    g.drawText(versionLine(), head.removeFromTop(20.0f * s), juce::Justification::centredLeft);
}

void AboutPanel::resized() {
    const float s = scaleOf(*this);
    auto card = cardBounds().reduced(static_cast<int>(24.0f * s), static_cast<int>(20.0f * s));
    close_.setBounds(card.getRight() - static_cast<int>(20.0f * s), card.getY(), static_cast<int>(20.0f * s), static_cast<int>(20.0f * s));
    card.removeFromTop(static_cast<int>(74.0f * s));
    link_.setFont(TetraLookAndFeel::font(14.0f * s, Weight::Medium), false, juce::Justification::centredLeft);
    link_.setBounds(card.removeFromTop(static_cast<int>(24.0f * s)).withWidth(static_cast<int>(200.0f * s)));
    card.removeFromTop(static_cast<int>(12.0f * s));
    auto tabs = card.removeFromTop(static_cast<int>(28.0f * s));
    aboutTab_.setBounds(tabs.removeFromLeft(static_cast<int>(96.0f * s)));
    tabs.removeFromLeft(static_cast<int>(6.0f * s));
    notesTab_.setBounds(tabs.removeFromLeft(static_cast<int>(110.0f * s)));
    card.removeFromTop(static_cast<int>(12.0f * s));
    viewport_.setBounds(card);
    rebuildPages();
}

void AboutPanel::showTab(int tab) {
    tab_ = tab;
    aboutTab_.setToggleState(tab == 0, juce::dontSendNotification);
    notesTab_.setToggleState(tab == 1, juce::dontSendNotification);
    builtScale_ = 0.0f;
    rebuildPages();
    viewport_.setViewPosition(0, 0);
}

void AboutPanel::rebuildPages() {
    const float s = scaleOf(*this);
    const int width = viewport_.getWidth() - 10;
    if (width <= 0) return;
    juce::AttributedString t;
    std::vector<TextLink> links;
    t.setLineSpacing(3.0f * s);
    const auto body = TetraLookAndFeel::font(13.5f * s);
    const auto heading = TetraLookAndFeel::font(14.5f * s, Weight::SemiBold);
    const auto small = TetraLookAndFeel::font(12.0f * s);

    if (tab_ == 0) {
        appendWithLinks(t, links, about_["description"].toString() + "\n\n", body, colours::textPrimary);
        appendWithLinks(t, links, about_["credits"].toString() + "\n\n", body, colours::textSecondary);
        t.append("Licenses\n", heading, colours::textPrimary);
        if (auto* list = about_["licenses"].getArray())
            for (const auto& item : *list) {
                t.append(item["title"].toString() + "\n", TetraLookAndFeel::font(13.0f * s, Weight::Medium), colours::textPrimary);
                appendWithLinks(t, links, item["text"].toString() + "\n\n", small, colours::textSecondary);
            }
        appendWithLinks(t, links, about_["copyright"].toString(), small, colours::textSecondary);
    } else {
        bool started = false;
        for (auto line : juce::StringArray::fromLines(changelog_)) {
            line = line.trimEnd();
            if (line.startsWith("## [")) {
                const auto version = line.fromFirstOccurrenceOf("[", false, false).upToFirstOccurrenceOf("]", false, false);
                const auto date = line.fromFirstOccurrenceOf("]", false, false).trimCharactersAtStart(" -").trim();
                t.append(juce::String(started ? "\n" : "") + "Version " + version, heading, colours::textPrimary);
                t.append(date.isNotEmpty() ? "   " + date + "\n" : juce::String("\n"), small, colours::textSecondary);
                started = true;
            } else if (started && line.startsWith("- ")) {
                t.append(juce::String(juce::CharPointer_UTF8("\xe2\x80\xa2  ")) + line.substring(2) + "\n", body, colours::textPrimary);
            } else if (started && line.isNotEmpty()) {
                t.append(line + "\n", body, colours::textSecondary);
            }
        }
    }
    page_->setText(t, width, std::move(links));
    builtScale_ = s;
}

int AboutPanel::numLinkAreas() const { return page_->numLinkAreas(); }

void AboutPanel::mouseDown(const juce::MouseEvent& e) {
    if (!cardBounds().contains(e.getPosition()) && onClose) onClose();
}

bool AboutPanel::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) {
        if (onClose) onClose();
        return true;
    }
    return false;
}

void AboutPanel::visibilityChanged() {
    if (isVisible()) grabKeyboardFocus();
}

}  // namespace tdui
