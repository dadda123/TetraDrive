/*
  ==============================================================================

   Local patch (TetraDrive): sample-accurate parameter points for VST3.

  ==============================================================================
*/

namespace juce
{

// Implement on an AudioProcessor to receive every automation point with its sample offset.
struct SampleAccurateParameterSink
{
    virtual ~SampleAccurateParameterSink() = default;

    // Called on the audio thread before processBlock, once per queued point.
    virtual void addParameterPoint (AudioProcessorParameter& parameter, int sampleOffset, float value) noexcept = 0;
};

} // namespace juce
