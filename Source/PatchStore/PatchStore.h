#pragma once

#include "../InstrumentMap/InstrumentMap.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace looper {

/** Optional APVTS-style float snapshot stored inside a patch for convenience. */
using PatchParams = std::unordered_map<std::string, double>;

/**
 * Patch JSON sidecar (schema v1).
 * Samples are referenced by relative paths (preferred) or absolute; audio is never embedded.
 */
struct Patch
{
    int schemaVersion = 1;
    std::string name;
    std::string patchRoot = ".";
    std::vector<SampleRef> samples;
    InstrumentMap map;
    PatchParams params;
};

struct PatchLoadResult
{
    Patch patch;
    /** Absolute paths that could not be resolved / opened after load. */
    std::vector<std::string> missingSamplePaths;
    /** Sample ids whose files were missing (map zones still present). */
    std::vector<std::string> offlineSampleIds;
};

class PatchStore
{
public:
    static constexpr int kSchemaVersion = 1;

    /** Deterministic JSON string (stable key order). */
    static std::string toJson(const Patch& patch);

    /** Parse schema v1 JSON. Returns nullopt on hard failure. */
    static std::optional<Patch> fromJson(const std::string& json);

    /**
     * Write JSON to path. Rewrites sample paths relative to the file's parent directory
     * when possible (mutates a copy; caller's Patch is unchanged).
     */
    static bool save(const std::string& jsonPath, const Patch& patch);

    /**
     * Load JSON from path. Resolves relative sample paths against the file's parent dir.
     * Does not decode audio — caller loads buffers.
     */
    static std::optional<PatchLoadResult> load(const std::string& jsonPath);

    // --- Path helpers (unit-tested) ---

    /** True if path looks absolute (POSIX /… or Windows drive / UNC). */
    static bool isAbsolutePath(const std::string& path);

    /**
     * Make path relative to baseDir when both are absolute and share a prefix.
     * Returns original path if not possible. Uses '/' separators in output.
     */
    static std::string makeRelativeTo(const std::string& path, const std::string& baseDir);

    /**
     * If path is absolute, return as-is (normalized separators).
     * Otherwise join with baseDir.
     */
    static std::string resolveAgainst(const std::string& path, const std::string& baseDir);

    /** Directory containing filePath (empty if none). */
    static std::string parentDirectory(const std::string& filePath);

    /** Normalize separators to '/' and collapse . / .. where safe. */
    static std::string normalizePath(const std::string& path);

    /** Apply relative-path rewrite on all sample paths for saving next to patchDir. */
    static Patch withRelativeSamplePaths(Patch patch, const std::string& patchDir);

    /** Resolve all sample paths against patchDir (in place). */
    static void resolveSamplePaths(Patch& patch, const std::string& patchDir);

    static std::string velCurveToString(VelCurve c);
    static std::optional<VelCurve> velCurveFromString(const std::string& s);
    static std::string modWheelTargetToString(ModWheelTarget t);
    static std::optional<ModWheelTarget> modWheelTargetFromString(const std::string& s);
};

} // namespace looper
