#include "ImportController.h"
#include "FileStreamSource.h"
#include <algorithm>
#include <cstdio>
#include <functional>

namespace looper {
namespace {

SampleBuffer toInterleaved(const juce::AudioBuffer<float>& planar, double sr)
{
    SampleBuffer out;
    out.channels = std::min(2, std::max(1, planar.getNumChannels()));
    out.sampleRate = sr > 0.0 ? sr : 44100.0;
    out.length = planar.getNumSamples();
    out.residentFrames = out.length;
    out.interleaved.resize((size_t) (out.length * out.channels));
    FileStreamSource::interleave(planar, out.channels, planar.getNumSamples(), out.interleaved.data());
    return out;
}

} // namespace

ImportController::ImportController() { prepareFormats(); }

void ImportController::prepareFormats()
{
    if (formatsReady_) return;
    formatManager_.registerBasicFormats();
    formatsReady_ = true;
}

bool ImportController::isSupportedAudioExtension(const juce::String& ext)
{
    const auto e = ext.trimCharactersAtStart(".").toLowerCase();
    return e == "wav" || e == "aiff" || e == "aif" || e == "flac";
}

juce::Array<juce::File> ImportController::collectAudioFiles(const juce::StringArray& paths)
{
    juce::Array<juce::File> files;
    for (const auto& p : paths) files.add(juce::File(p));
    return collectAudioFiles(files);
}

juce::Array<juce::File> ImportController::collectAudioFiles(const juce::Array<juce::File>& filesOrFolders)
{
    juce::Array<juce::File> out;
    juce::StringArray seen;
    std::function<void(const juce::File&)> visit = [&](const juce::File& f) {
        if (!f.exists()) return;
        if (f.isDirectory()) {
            for (const auto& c : f.findChildFiles(juce::File::findFilesAndDirectories, false))
                visit(c);
            return;
        }
        if (!isSupportedAudioExtension(f.getFileExtension())) return;
        const auto key = f.getFullPathName();
        if (seen.contains(key)) return;
        seen.add(key);
        out.add(f);
    };
    for (const auto& f : filesOrFolders) visit(f);
    out.sort();
    return out;
}

std::string ImportController::makeSampleId(const juce::File& file)
{
    const auto path = file.getFullPathName().toStdString();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "s_%08x", (uint32_t) std::hash<std::string>{}(path));
    return buf;
}

LoadedSample ImportController::loadFileIntoPool(const juce::File& file, SamplePool& pool)
{
    SampleRef stub;
    stub.id = makeSampleId(file);
    stub.path = file.getFullPathName().toStdString();
    stub.displayName = file.getFileName().toStdString();
    return loadFileIntoPool(file, pool, stub);
}

LoadedSample ImportController::loadFileIntoPool(const juce::File& file, SamplePool& pool, const SampleRef& preserve)
{
    prepareFormats();
    LoadedSample result;
    result.ref = preserve;
    if (result.ref.id.empty())
        result.ref.id = makeSampleId(file);
    result.ref.path = file.getFullPathName().toStdString();
    if (result.ref.displayName.empty())
        result.ref.displayName = file.getFileName().toStdString();

    SampleBuffer buf;
    if (!decodeFile(file, buf, result.error))
        return result;
    result.ref.durationSamples = buf.length;
    result.ref.sampleRate = buf.sampleRate;
    result.ref.channels = buf.channels;
    pitchCache_.erase(result.ref.id); // new audio → re-analyse on next import
    pool.addStub(result.ref);
    pool.setBuffer(result.ref.id, std::move(buf));
    result.ok = true;
    return result;
}

bool ImportController::decodeFile(const juce::File& file, SampleBuffer& out, juce::String& error)
{
    prepareFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
    if (!reader) { error = "Unsupported: " + file.getFileName(); return false; }
    if (reader->lengthInSamples <= 0) { error = "Empty: " + file.getFileName(); return false; }

    constexpr int64_t kMax = 48000LL * 60 * 30;
    const int64_t total = std::min<int64_t>(reader->lengthInSamples, kMax);
    const int64_t preload = std::max<int64_t>(1, streaming_.preloadFrames);
    // Short samples (<= preload) and "Load fully into RAM" decode everything.
    const bool stream = !streaming_.loadFully && total > preload;
    const int n = (int) (stream ? preload : total);

    juce::AudioBuffer<float> planar(std::max(1, (int) reader->numChannels), n);
    if (!reader->read(&planar, 0, n, 0, true, true)) { error = "Unreadable: " + file.getFileName(); return false; }
    if (planar.getNumChannels() > 2) FileStreamSource::downmixToStereo(planar);

    out = toInterleaved(planar, reader->sampleRate);
    if (stream)
    {
        out.length = total;
        out.residentFrames = n;
        out.stream = std::make_shared<FileStreamSource>(file, out.channels, total);
    }
    return true;
}

bool ImportController::importFiles(const juce::Array<juce::File>& files, SamplePool& pool,
                                   const std::vector<SampleRef>& existingUserRefs,
                                   AutoMapOptions options)
{
    auto audio = collectAudioFiles(files);
    if (audio.isEmpty() && existingUserRefs.empty()) { clearPending(); return false; }

    std::vector<SampleRef> refs = existingUserRefs;
    SamplePool::ScopedBatch batch(pool); // one snapshot for the whole import
    for (const auto& f : audio) {
        auto loaded = loadFileIntoPool(f, pool);
        if (!loaded.ok) continue;
        auto it = std::find_if(refs.begin(), refs.end(),
                               [&](const SampleRef& r) { return r.id == loaded.ref.id; });
        if (it != refs.end()) *it = loaded.ref;
        else refs.push_back(loaded.ref);
    }
    if (refs.empty()) { clearPending(); return false; }

    // Pitch-detect every sample (cached per id, ~3 ms each). Samples without a filename
    // note use the result for their root key; named samples only for the filename/audio
    // mismatch flag (filename still wins). Runs on the calling (message) thread.
    PitchAnalysisMap analyses;
    for (auto& ref : refs) {
        const auto buffer = pool.getBuffer(ref.id);
        if (!buffer)
            continue;
        analyses[ref.id] = analysePitch(ref.id, *buffer);
    }

    pendingRefs_ = std::move(refs);
    pendingOptions_ = options;
    pendingAnalyses_ = std::move(analyses);
    pending_ = AutoMapper::map(pendingRefs_, pendingOptions_, &pendingAnalyses_);
    applyPitchMetadata(pendingRefs_, *pending_);
    return true;
}

bool ImportController::useDetectedPitch(const std::string& sampleId)
{
    if (!pending_)
        return false;
    const auto& reviews = pending_->reviews;
    const auto it = std::find_if(reviews.begin(), reviews.end(),
                                 [&](const SampleReview& r) { return r.sampleId == sampleId; });
    if (it == reviews.end() || !AutoMapper::canUseDetected(*it))
        return false;
    pendingOptions_.useDetectedFor.insert(sampleId);
    pending_ = AutoMapper::map(pendingRefs_, pendingOptions_, &pendingAnalyses_);
    applyPitchMetadata(pendingRefs_, *pending_);
    return true;
}

PitchAnalysis ImportController::analysePitch(const std::string& sampleId, const SampleBuffer& buffer)
{
    if (const auto it = pitchCache_.find(sampleId); it != pitchCache_.end())
        return it->second;
    PitchAnalysis a;
    if (buffer.isStreaming())
    {
        // Streamed sample: analyse the first few seconds (the detector looks at <= 1.5 s after
        // the onset), read through the stream like playback would.
        const int ch = std::max(1, buffer.channels);
        const int64_t want = std::min<int64_t>(buffer.length,
                                               std::max<int64_t>(buffer.residentLength(),
                                                                 (int64_t) (buffer.sampleRate * 8.0)));
        std::vector<float> window((size_t) (want * ch));
        const int64_t got = buffer.copyFrames(0, want, window.data());
        if (got > 0)
            a = PitchDetector::analyzeInterleaved(window.data(), (std::size_t) got, ch, buffer.sampleRate);
        else {
            a.analysed = true;
            a.reason = "no audio";
        }
        pitchCache_[sampleId] = a;
        return a;
    }
    const auto frames = static_cast<std::size_t>(std::max<int64_t>(0, buffer.length));
    const auto needed = frames * static_cast<std::size_t>(std::max(1, buffer.channels));
    if (frames > 0 && buffer.interleaved.size() >= needed)
        a = PitchDetector::analyzeInterleaved(buffer.interleaved.data(), frames,
                                              buffer.channels, buffer.sampleRate);
    else {
        a.analysed = true;
        a.reason = "no audio";
    }
    pitchCache_[sampleId] = a;
    return a;
}

} // namespace looper
