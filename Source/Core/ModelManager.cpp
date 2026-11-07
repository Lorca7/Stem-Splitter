#include "ModelManager.h"

#include <juce_events/juce_events.h>

#ifndef STEMSPLITTER_MODEL_BASE_URL
 #define STEMSPLITTER_MODEL_BASE_URL "https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/main"
#endif

namespace stemsplitter
{

namespace ModelManager
{

juce::StringArray getRequiredFiles (EngineType engine)
{
    switch (engine)
    {
        case EngineType::fourStems:
            return { "ggml-model-htdemucs-4s-f16.bin" };

        case EngineType::sixStems:
            return { "ggml-model-htdemucs-6s-f16.bin" };

        case EngineType::fourStemsHQ:
            return { "ggml-model-htdemucs_ft_drums-4s-f16.bin",
                     "ggml-model-htdemucs_ft_bass-4s-f16.bin",
                     "ggml-model-htdemucs_ft_other-4s-f16.bin",
                     "ggml-model-htdemucs_ft_vocals-4s-f16.bin" };

        case EngineType::lite:
        case EngineType::numEngines:
            break;
    }

    return {};
}

juce::int64 getDownloadSize (EngineType engine)
{
    constexpr juce::int64 mb = 1024 * 1024;

    switch (engine)
    {
        case EngineType::fourStems:   return 81 * mb;
        case EngineType::sixStems:    return 53 * mb;
        case EngineType::fourStemsHQ: return 4 * 81 * mb;
        case EngineType::lite:
        case EngineType::numEngines:  break;
    }

    return 0;
}

juce::File getUserModelDirectory()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support/Stem Splitter/Models");
   #elif JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Stem Splitter/Models");
   #else
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
               .getChildFile (".local/share/StemSplitter/models");
   #endif
}

juce::Array<juce::File> getSearchDirectories()
{
    juce::Array<juce::File> dirs;

    auto add = [&dirs] (const juce::File& d)
    {
        if (d != juce::File() && ! dirs.contains (d))
            dirs.add (d);
    };

    const auto env = juce::SystemStats::getEnvironmentVariable ("STEMSPLITTER_MODEL_DIR", {});

    if (env.isNotEmpty() && juce::File::isAbsolutePath (env))
        add (juce::File (env));

    // Inside our own bundle: <bundle>/Contents/MacOS/<binary> → <bundle>/Contents/Resources/Models
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
   #if JUCE_MAC
    add (exe.getParentDirectory().getSiblingFile ("Resources").getChildFile ("Models"));
   #endif
    add (exe.getParentDirectory().getChildFile ("Models"));
    add (exe.getParentDirectory().getChildFile ("models"));

    add (getUserModelDirectory());

   #if JUCE_MAC
    add (juce::File ("/Library/Application Support/Stem Splitter/Models"));
    add (juce::File ("/Applications/Stem Splitter.app/Contents/Resources/Models"));
    add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
             .getChildFile ("Applications/Stem Splitter.app/Contents/Resources/Models"));
   #endif

    return dirs;
}

juce::File findModelFile (const juce::String& fileName)
{
    for (const auto& dir : getSearchDirectories())
    {
        auto f = dir.getChildFile (fileName);

        if (f.existsAsFile() && f.getSize() > 1024 * 1024)
            return f;
    }

    return {};
}

bool isInstalled (EngineType engine)
{
    for (const auto& name : getRequiredFiles (engine))
        if (findModelFile (name) == juce::File())
            return false;

    return true;
}

juce::URL getDownloadUrl (const juce::String& fileName)
{
    return juce::URL (juce::String (STEMSPLITTER_MODEL_BASE_URL) + "/" + fileName);
}

bool looksLikeModelFile (const juce::File& f)
{
    if (! f.existsAsFile() || ! f.hasFileExtension ("bin") || f.getSize() < 1024 * 1024)
        return false;

    juce::FileInputStream in (f);

    if (in.failedToOpen())
        return false;

    const auto magic = (juce::uint32) in.readInt();  // little-endian, as written by the converter
    return magic == 0x646d6334u     // "dmc4"
        || magic == 0x646d6336u;    // "dmc6"
}

int importModelFiles (const juce::Array<juce::File>& files, juce::String& error)
{
    const auto dest = getUserModelDirectory();

    if (! dest.createDirectory())
    {
        error = "Couldn't create " + dest.getFullPathName();
        return 0;
    }

    int imported = 0;

    for (const auto& f : files)
    {
        if (! looksLikeModelFile (f))
        {
            error = f.getFileName() + " isn't a Demucs model file.";
            continue;
        }

        const auto target = dest.getChildFile (f.getFileName());

        if (target == f || f.copyFileTo (target))
            ++imported;
        else
            error = "Couldn't copy " + f.getFileName();
    }

    return imported;
}

} // namespace ModelManager

//==============================================================================
ModelDownloader::ModelDownloader() : juce::Thread ("Stem Splitter model download") {}

ModelDownloader::~ModelDownloader()
{
    masterReference.clear();
    stopThread (10000);
}

void ModelDownloader::start (EngineType e)
{
    if (isThreadRunning())
        return;

    engine = e;
    progress = 0.0f;

    {
        const juce::ScopedLock sl (errorLock);
        lastError.clear();
    }

    startThread();
}

void ModelDownloader::cancel()
{
    signalThreadShouldExit();
}

void ModelDownloader::run()
{
    juce::String error;
    const auto files = ModelManager::getRequiredFiles (engine);
    const auto total = ModelManager::getDownloadSize (engine);
    const auto perFile = files.isEmpty() ? (juce::int64) 0 : total / files.size();
    juce::int64 done = 0;

    for (const auto& name : files)
    {
        if (ModelManager::findModelFile (name) == juce::File())
            error = downloadFile (name, done, total);

        if (error.isNotEmpty() || threadShouldExit())
            break;

        done += perFile;
    }

    if (threadShouldExit() && error.isEmpty())
        error = "Download cancelled.";

    if (error.isEmpty())
        progress = 1.0f;

    {
        const juce::ScopedLock sl (errorLock);
        lastError = error == "Download cancelled." ? juce::String() : error;
    }

    juce::MessageManager::callAsync ([weakThis = juce::WeakReference<ModelDownloader> (this), e = engine, error]
    {
        if (weakThis != nullptr && weakThis->onFinished)
            weakThis->onFinished (e, error);
    });
}

juce::String ModelDownloader::downloadFile (const juce::String& fileName, juce::int64 bytesBefore, juce::int64 totalBytes)
{
    const auto dir = ModelManager::getUserModelDirectory();

    if (! dir.createDirectory())
        return "Couldn't create the folder " + dir.getFullPathName();

    const auto target = dir.getChildFile (fileName);
    const auto partial = dir.getChildFile (fileName + ".download");
    partial.deleteFile();

    int statusCode = 0;
    juce::StringPairArray headers;

    auto stream = ModelManager::getDownloadUrl (fileName)
                      .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                              .withConnectionTimeoutMs (20000)
                                              .withNumRedirectsToFollow (10)
                                              .withStatusCode (&statusCode)
                                              .withResponseHeaders (&headers));

    if (stream == nullptr)
        return "Couldn't connect to the model server. Check your internet connection.";

    if (statusCode != 0 && statusCode != 200)
        return "The model server returned an error (HTTP " + juce::String (statusCode) + ").";

    juce::FileOutputStream out (partial);

    if (out.failedToOpen())
        return "Couldn't write to " + partial.getFullPathName();

    const auto expected = stream->getTotalLength();
    juce::HeapBlock<char> buffer (1 << 16);
    juce::int64 received = 0;

    while (! stream->isExhausted())
    {
        if (threadShouldExit())
        {
            out.flush();
            partial.deleteFile();
            return {};
        }

        const auto n = stream->read (buffer.getData(), 1 << 16);

        if (n < 0)
            break;

        if (n == 0)
        {
            if (stream->isExhausted())
                break;

            continue;
        }

        if (! out.write (buffer.getData(), (size_t) n))
        {
            partial.deleteFile();
            return "Writing the model failed. Is the disk full?";
        }

        received += n;
        const auto fileTotal = expected > 0 ? expected : totalBytes;

        if (totalBytes > 0)
            progress = juce::jlimit (0.0f, 0.999f,
                                     (float) (bytesBefore + juce::jmin (received, fileTotal)) / (float) totalBytes);
    }

    out.flush();

    if (expected > 0 && received != expected)
    {
        partial.deleteFile();
        return "The download was interrupted. Please try again.";
    }

    if (! partial.moveFileTo (target))
    {
        partial.deleteFile();
        return "Couldn't save " + target.getFullPathName();
    }

    if (! ModelManager::looksLikeModelFile (target))
    {
        target.deleteFile();
        return "The downloaded file isn't a valid model. The download location may have changed.";
    }

    return {};
}

} // namespace stemsplitter
