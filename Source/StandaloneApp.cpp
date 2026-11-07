// Custom standalone application: a native macOS window (instead of JUCE's default title bar with an
// "Options" button), output-only audio, and audio settings reachable from the app's own UI.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "PluginProcessor.h"

namespace stemsplitter
{

class StemSplitterApp final : public juce::JUCEApplication
{
public:
    StemSplitterApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Stem Splitter";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
       #else
        options.folderName = "Stem Splitter";
       #endif
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override       { return "Stem Splitter"; }
    const juce::String getApplicationVersion() override    { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        openFromCommandLine (commandLine);
    }

    void initialise (const juce::String& commandLine) override
    {
        // Output only: the app plays back the song, it never records, so no microphone permission
        // prompt and no feedback warning.
        juce::Array<juce::StandalonePluginHolder::PluginInOuts> channels;
        channels.add ({ 0, 2 });

        auto holder = std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(), false, juce::String{},
                                                                      nullptr, channels, false);

        window = std::make_unique<juce::StandaloneFilterWindow> (getApplicationName(), juce::Colour (0xff0c0d10), std::move (holder));
        window->setUsingNativeTitleBar (true);
        window->setResizable (true, false);

        if (auto* processor = dynamic_cast<StemSplitterProcessor*> (window->getAudioProcessor()))
        {
            auto* pluginHolder = window->getPluginHolder();
            processor->showAudioSettings = [pluginHolder] { pluginHolder->showAudioSettingsDialog(); };

            // The editor was built before the callback existed; let it pick up the settings button.
            processor->sendChangeMessage();
        }

        window->setVisible (true);
        openFromCommandLine (commandLine);
    }

    void shutdown() override
    {
        if (window != nullptr)
            window->pluginHolder->savePluginState();

        window = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (window != nullptr)
            window->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    /** Opening a file from Finder ("Open With") or the command line loads it. */
    void openFromCommandLine (const juce::String& commandLine)
    {
        if (window == nullptr)
            return;

        for (auto& arg : juce::StringArray::fromTokens (commandLine, true))
        {
            const juce::File f (arg.unquoted());

            if (f.existsAsFile())
                if (auto* processor = dynamic_cast<StemSplitterProcessor*> (window->getAudioProcessor()))
                {
                    processor->loadFile (f);
                    return;
                }
        }
    }

    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> window;
};

} // namespace stemsplitter

START_JUCE_APPLICATION (stemsplitter::StemSplitterApp)

#endif
