#pragma once

namespace daw::testplugins
{

/** Registers the Audio Unit test plugins (v2) in this process only, as Apple's
    AudioComponentRegister() allows: "DAW Tests: DAW Test Gain" (aufx DtGn DAWt),
    "... Latency" (aufx DtLt), "... NaN" (aufx DtNn) and "... Synth" (aumu DtSy). Nothing
    is installed; other processes never see them. Safe to call more than once. */
void registerTestAudioUnits();

} // namespace daw::testplugins
