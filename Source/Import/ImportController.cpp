#include "ImportController.h"
#include <algorithm>
#include <cstdio>
#include <functional>

namespace looper {
namespace {

void downmixToStereo(juce::AudioBuffer<float>& buffer)
{
    const int ch = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    if (ch <= 2 || n <= 0) return;
    juce::AudioBuffer<float> stereo(2, n);
    stereo.clear();
    const float scale = 1.0f / (float) ch;
    for (int c = 0; c < ch; ++c)
        stereo.addFrom(c % 2, 0, buffer, c, 0, n, scale);
    buffer = std::move(stereo);
}

SampleBuffer toInterleaved(const juce::AudioBuffer<float>& planar, double sr)
{
    SampleBuffer out;
    out.channels = std::min(2, std::max(1, planar.getNumChannels()));
    out.sampleRate = sr > 0.0 ? sr : 44100.0;
    out.length = planar.getNumSamples();
    out.interleaved.resize((size_t) (out.length * out.channels));
    for (int i = 0; i < planar.getNumSamples(); ++i)
        for (int c = 0; c < out.channels; ++c)
            out.interleaved[(size_t) (i * out.channels + c)] =
                planar.getReadPointer(std::min(c, planar.getNumChannels() - 1))[i];
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

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
    if (!reader) { result.error = "Unsupported: " + file.getFileName(); return result; }
    if (reader->lengthInSamples <= 0) { result.error = "Empty: " + file.getFileName(); return result; }

    constexpr int64_t kMax = 48000LL * 60 * 30;
    const int n = (int) std::min<juce::int64>(reader->lengthInSamples, kMax);
    juce::AudioBuffer<float> planar(std::max(1, (int) reader->numChannels), n);
    reader->read(&planar, 0, n, 0, true, true);
    if (planar.getNumChannels() > 2) downmixToStereo(planar);

    auto buf = toInterleaved(planar, reader->sampleRate);
    result.ref.durationSamples = buf.length;
    result.ref.sampleRate = buf.sampleRate;
    result.ref.channels = buf.channels;
    pitchCache_.erase(result.ref.id); // new audio → re-analyse on next import
    pool.addStub(result.ref);
    pool.setBuffer(result.ref.id, std::move(buf));
    result.ok = true;
    return result;
}

bool ImportController::importFiles(const juce::Array<juce::File>& files, SamplePool& pool,
                                   const std::vector<SampleRef>& existingUserRefs,
                                   AutoMapOptions options)
{
    auto audio = collectAudioFiles(files);
    if (audio.isEmpty() && existingUserRefs.empty()) { clearPending(); return false; }

    std::vector<SampleRef> refs = existingUserRefs;
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
    const auto frames = static_cast<std::size_t>(std::max<int64_t>(0, buffer.length));
    const auto needed = frames * static_cast<std::size_t>(std::max(1, buffer.channels));
    PitchAnalysis a;
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
