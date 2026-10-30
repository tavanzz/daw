// Test entry point: runs Catch2 with JUCE set up the way an application has it.
// Some JUCE classes (the AudioDeviceManager, for one) must be used from the message
// thread, and JUCE's singletons must be cleaned up before exit.

#include <juce_events/juce_events.h>

#include <catch2/catch_session.hpp>

int main(int argc, char* argv[])
{
    juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();

    const int result = Catch::Session().run(argc, argv);

    juce::DeletedAtShutdown::deleteAll();
    juce::MessageManager::deleteInstance();
    return result;
}
