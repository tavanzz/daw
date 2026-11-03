// Test fixture, never compiled: breaks the rule "components hold no literal colours
// or fonts".
void paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff123456));
    g.setFont(juce::FontOptions(12.0f));
    g.fillAll(juce::Colours::transparentBlack); // allowed
}
