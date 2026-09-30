// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Processor.h"
namespace sineweave {
class NumericSlider final : public juce::Slider {
  public:
    bool keyPressed(const juce::KeyPress &key) override {
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::F2Key) {
            showTextBox();
            return true;
        }
        return juce::Slider::keyPressed(key);
    }
};
class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
    class Controls final : public juce::Component {
        void focusOfChildComponentChanged(juce::Component::FocusChangeType) override {
            auto *focused = juce::Component::getCurrentlyFocusedComponent();
            auto *view = findParentComponentOfClass<juce::Viewport>();
            if (!focused || !view || !isParentOf(focused))
                return;
            const auto bounds = getLocalArea(focused, focused->getLocalBounds());
            const auto visible = view->getViewArea();
            if (bounds.getY() < visible.getY())
                view->setViewPosition(0, bounds.getY());
            else if (bounds.getBottom() > visible.getBottom())
                view->setViewPosition(0, bounds.getBottom() - visible.getHeight());
        }
    } content;
    juce::Viewport viewport;
    Processor &processor;
    juce::TextButton load{"Load audio file..."}, analyse{"Analyse / Re-analyse"};
    juce::ToggleButton preview{"Preview sustain changes"};
    juce::Label instructions, channelLabel;
    juce::ComboBox channel;
    std::array<juce::Label, 3> choiceLabels;
    std::array<juce::ComboBox, 3> choices;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>, 3> choiceAttachments;
    std::array<juce::Label, specs.size()> labels;
    std::array<NumericSlider, specs.size()> sliders;
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::array<std::unique_ptr<Attachment>, specs.size()> attachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> channelAttachment;
    juce::TextEditor status;
    std::unique_ptr<juce::FileChooser> chooser;
    void timerCallback() override;

  public:
    explicit Editor(Processor &);
    ~Editor() override;
    void resized() override;
};
} // namespace sineweave
