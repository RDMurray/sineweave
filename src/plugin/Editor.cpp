// SPDX-License-Identifier: AGPL-3.0-only
#include "Editor.h"
namespace sineweave {
Editor::Editor(Processor &p) : AudioProcessorEditor(p), processor(p) {
    setTitle("Sineweave additive instrument");
    setSize(780, 780);
    instructions.setText(
        "Load file > set analysis range > Analyse > set sustain position and reference pitch > play "
        "MIDI. Choose Trajectory to follow the analysis.\nTimes are original-file seconds. Analysis "
        "range/settings require Re-analyse.",
        juce::dontSendNotification);
    addAndMakeVisible(instructions);
    addAndMakeVisible(load);
    load.setExplicitFocusOrder(1);
    load.setTooltip("Choose an audio file using the native Windows dialog.");
    load.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Load audio sample", juce::File{},
                                                      "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", true);
        juce::Component::SafePointer<Editor> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                             [safe](const juce::FileChooser &c) {
                                 if (safe && c.getResult().existsAsFile())
                                     safe->processor.loadFile(c.getResult());
                             });
    };
    addAndMakeVisible(analyse);
    analyse.setExplicitFocusOrder(2);
    analyse.onClick = [this] { processor.analyse(); };
    analyse.setTooltip("Analyse the chosen range in the background. Previous timbre remains playable.");
    preview.setToggleState(true, juce::dontSendNotification);
    preview.setExplicitFocusOrder(3);
    preview.setTooltip(
        "Play a short resynthesis preview after changing sustain position while transport is stopped.");
    preview.onClick = [this] { processor.previewEnabled.store(preview.getToggleState()); };
    addAndMakeVisible(preview);
    processor.previewEnabled.store(true);
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setExplicitFocusOrder(4);
    addAndMakeVisible(viewport);
    channelLabel.setText("Analysis input channel", juce::dontSendNotification);
    content.addAndMakeVisible(channelLabel);
    channel.addItemList({"Mono mix", "Left", "Right"}, 1);
    channel.setTitle("Analysis input channel");
    channel.setExplicitFocusOrder(1);
    content.addAndMakeVisible(channel);
    channelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.parameters, "channel", channel);
    const char *choiceNames[] = {"Playback mode", "Initial direction", "Loop mode"};
    const char *choiceIds[] = {"playMode", "direction", "loopMode"};
    const juce::StringArray choiceItems[] = {
        {"Frozen frame", "Trajectory"}, {"Forward", "Reverse"}, {"Off", "Wrap", "Ping-pong"}};
    for (int i = 0; i < 3; ++i) {
        choiceLabels[i].setText(choiceNames[i], juce::dontSendNotification);
        content.addAndMakeVisible(choiceLabels[i]);
        choices[i].addItemList(choiceItems[i], 1);
        choices[i].setTitle(choiceNames[i]);
        choices[i].setExplicitFocusOrder(i + 2);
        choices[i].setTooltip("Captured by new MIDI notes. Held notes retain their playback configuration.");
        content.addAndMakeVisible(choices[i]);
        choiceAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            processor.parameters, choiceIds[i], choices[i]);
    }
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const auto &s = specs[i];
        labels[i].setText(s.name, juce::dontSendNotification);
        content.addAndMakeVisible(labels[i]);
        auto &slider = sliders[i];
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 185, 26);
        slider.setWantsKeyboardFocus(true);
        slider.setTitle(juce::String(s.name) + " (" + s.unit + ")");
        slider.setDescription("Editable numeric parameter. Use arrow keys; Enter or F2 edits the value.");
        slider.setExplicitFocusOrder(static_cast<int>(i) + 5);
        content.addAndMakeVisible(slider);
        attachments[i] = std::make_unique<Attachment>(processor.parameters, s.id, slider);
    }
    status.setMultiLine(true);
    status.setReadOnly(true);
    status.setTitle("Analysis status");
    status.setDescription("Read analysis completion, errors, and partial counts here.");
    status.setWantsKeyboardFocus(true);
    status.setExplicitFocusOrder(5);
    status.setText(processor.status());
    addAndMakeVisible(status);
    startTimerHz(5);
}
Editor::~Editor() {
    processor.previewEnabled.store(false);
    stopTimer();
    chooser.reset();
}
void Editor::timerCallback() {
    const auto text = processor.status();
    if (text != status.getText())
        status.setText(text, false);
}
void Editor::resized() {
    auto area = getLocalBounds().reduced(16);
    instructions.setBounds(area.removeFromTop(58));
    auto commands = area.removeFromTop(36);
    load.setBounds(commands.removeFromLeft(220).reduced(2));
    analyse.setBounds(commands.removeFromLeft(220).reduced(2));
    preview.setBounds(commands.reduced(2));
    status.setBounds(area.removeFromBottom(105));
    area.removeFromBottom(10);
    viewport.setBounds(area);
    content.setSize(viewport.getWidth() - viewport.getScrollBarThickness(),
                    4 * 38 + static_cast<int>(specs.size()) * 36);
    auto controlsArea = content.getLocalBounds();
    auto row = controlsArea.removeFromTop(38);
    channelLabel.setBounds(row.removeFromLeft(285));
    channel.setBounds(row.reduced(2));
    for (int i = 0; i < 3; ++i) {
        row = controlsArea.removeFromTop(38);
        choiceLabels[i].setBounds(row.removeFromLeft(285));
        choices[i].setBounds(row.reduced(2));
    }
    for (std::size_t i = 0; i < specs.size(); ++i) {
        row = controlsArea.removeFromTop(36);
        labels[i].setBounds(row.removeFromLeft(285));
        sliders[i].setBounds(row);
    }
}
} // namespace sineweave
