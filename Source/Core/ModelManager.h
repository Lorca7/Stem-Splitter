#pragma once

#include "StemTypes.h"

namespace stemsplitter
{

/** Locates (and downloads) the neural-network weight files used by the Demucs engines.

    Models are looked for, in order, in:
      1. $STEMSPLITTER_MODEL_DIR
      2. the Models folder inside this app/plug-in bundle (Contents/Resources/Models)
      3. ~/Library/Application Support/Stem Splitter/Models   (where downloads go)
      4. /Library/Application Support/Stem Splitter/Models
      5. /Applications/Stem Splitter.app/Contents/Resources/Models (so the plug-ins can share
         the models shipped with the standalone app)
*/
namespace ModelManager
{
    /** File names needed for an engine (empty for the Lite engine). */
    juce::StringArray getRequiredFiles (EngineType engine);

    /** Approximate download size in bytes. */
    juce::int64 getDownloadSize (EngineType engine);

    juce::Array<juce::File> getSearchDirectories();

    /** Where downloaded models are stored. */
    juce::File getUserModelDirectory();

    /** Returns the model file if installed anywhere, else an invalid File. */
    juce::File findModelFile (const juce::String& fileName);

    bool isInstalled (EngineType engine);

    juce::URL getDownloadUrl (const juce::String& fileName);

    /** Copies user-supplied model files into the user model folder. Returns how many were valid. */
    int importModelFiles (const juce::Array<juce::File>& files, juce::String& error);

    bool looksLikeModelFile (const juce::File& f);
}

//==============================================================================
/** Downloads the files of one engine in the background. Poll its state from any thread. */
class ModelDownloader : private juce::Thread
{
public:
    ModelDownloader();
    ~ModelDownloader() override;

    void start (EngineType engine);
    void cancel();

    bool isDownloading() const noexcept            { return isThreadRunning(); }
    float getProgress() const noexcept             { return progress.load(); }
    juce::String getLastError() const              { const juce::ScopedLock sl (errorLock); return lastError; }
    EngineType getEngine() const noexcept          { return engine; }

    /** Called on the message thread when finished. `error` is empty on success. */
    std::function<void (EngineType, juce::String error)> onFinished;

private:
    void run() override;
    juce::String downloadFile (const juce::String& fileName, juce::int64 bytesBefore, juce::int64 totalBytes);

    EngineType engine = EngineType::fourStems;
    std::atomic<float> progress { 0.0f };
    mutable juce::CriticalSection errorLock;
    juce::String lastError;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ModelDownloader)
};

} // namespace stemsplitter
