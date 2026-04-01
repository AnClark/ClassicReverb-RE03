#include "PresetManager.h"
#include "UI.h"

#include "../3rdparty/json.hpp"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#  include <windows.h>
#  include <shlobj.h>
#elif defined(__APPLE__)
#  include <unistd.h>
#  include <pwd.h>
#else
// Linux / other POSIX
#  include <unistd.h>
#  include <pwd.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

// ── Factory presets ────────────────────────────────────────────────────────
// Name, RoomSize(m²), Stereo, Damping, HFColour, EarlyMix, WetDry, OutputVolume, PreDelay(ms)
static const Preset kFactoryPresets[] = {
    {"Grand Hall",     400.0f, 0.85f, 0.12f, 0.10f, 0.35f, 0.45f, 0.5f,  18.0f},
    {"Small Room",       8.0f, 0.60f, 0.55f, 0.15f, 0.55f, 0.35f, 0.5f,   0.0f},
    {"Cathedral",      560.0f, 0.95f, 0.06f, 0.05f, 0.18f, 0.55f, 0.5f,  30.0f},
    {"Bright Chamber",  40.0f, 0.75f, 0.10f, 0.65f, 0.45f, 0.40f, 0.5f,   6.0f},
    {"Warm Plate",      72.0f, 0.80f, 0.65f, 0.02f, 0.28f, 0.48f, 0.5f,   0.0f},
    {"Long Ambience",  200.0f, 0.80f, 0.18f, 0.10f, 0.22f, 0.38f, 0.5f,  10.0f},
};
static constexpr int kFactoryPresetsCount = (int)(sizeof(kFactoryPresets) / sizeof(kFactoryPresets[0]));

// ── Constructor ────────────────────────────────────────────────────────────
PresetManager::PresetManager(ClassicReverbUI* ui)
    : fUI(ui)
{
}

// ── Factory preset accessors ───────────────────────────────────────────────
int PresetManager::factoryPresetCount() const
{
    return kFactoryPresetsCount;
}

const Preset& PresetManager::factoryPreset(int index) const
{
    DISTRHO_SAFE_ASSERT_RETURN(index >= 0 && index < kFactoryPresetsCount, kFactoryPresets[0])
    return kFactoryPresets[index];
}

// ── User preset accessors ──────────────────────────────────────────────────
int PresetManager::userPresetCount() const
{
    return (int)fUserPresets.size();
}

const Preset& PresetManager::userPreset(int index) const
{
    DISTRHO_SAFE_ASSERT_RETURN(index >= 0 && index < (int)fUserPresets.size(), fImportedPreset)
    return fUserPresets[index];
}

// ── Preset selection ───────────────────────────────────────────────────────
void PresetManager::loadDefaultPreset()
{
    DISTRHO_SAFE_ASSERT_RETURN(fUI != nullptr, )
    for (uint32_t i = 0; i < PARAM_COUNT; ++i)
        _triggerParamUpdate(i, kParamRanges[i].def);
    syncPluginState(PresetType::Factory, -1, false);
}

void PresetManager::selectFactoryPreset(int index)
{
    DISTRHO_SAFE_ASSERT_RETURN(index >= 0 && index < kFactoryPresetsCount, )
    _applyPreset(kFactoryPresets[index]);
    syncPluginState(PresetType::Factory, index, false);
}

void PresetManager::selectUserPreset(int index)
{
    DISTRHO_SAFE_ASSERT_RETURN(index >= 0 && index < (int)fUserPresets.size(), )
    _applyPreset(fUserPresets[index]);
    syncPluginState(PresetType::User, index, false);
}

void PresetManager::selectImportedPreset()
{
    _applyPreset(fImportedPreset);
    syncPluginState(PresetType::Imported, -1, false);
}

// ── User preset CRUD ──────────────────────────────────────────────────────
bool PresetManager::nameExists(const std::string& name) const
{
    for (const auto& p : fUserPresets)
        if (p.name == name) return true;
    return false;
}

bool PresetManager::saveAsNew(const std::string& name)
{
    if (nameExists(name))
        return false;

    Preset p  = snapshotFromUI();
    p.name    = name;
    fUserPresets.push_back(p);

    const bool ok = saveUserPresetsToDisk();
    syncPluginState(PresetType::User, (int)fUserPresets.size() - 1, false);
    return ok;
}

bool PresetManager::overwriteCurrent()
{
    if (fCurrentType != PresetType::User)
        return false;
    DISTRHO_SAFE_ASSERT_RETURN(fCurrentIndex >= 0 && fCurrentIndex < (int)fUserPresets.size(), false)

    const std::string name = fUserPresets[fCurrentIndex].name;
    Preset p = snapshotFromUI();
    p.name   = name;
    fUserPresets[fCurrentIndex] = p;

    const bool ok = saveUserPresetsToDisk();
    syncPluginState(false);
    return ok;
}

bool PresetManager::deleteCurrent()
{
    if (fCurrentType != PresetType::User)
        return false;
    DISTRHO_SAFE_ASSERT_RETURN(fCurrentIndex >= 0 && fCurrentIndex < (int)fUserPresets.size(), false)

    fUserPresets.erase(fUserPresets.begin() + fCurrentIndex);
    PresetType newType;
    int        newIndex;
    if (fUserPresets.empty()) {
        newType  = PresetType::Factory;
        newIndex = 0;
    } else {
        newType  = PresetType::User;
        newIndex = std::min(fCurrentIndex, (int)fUserPresets.size() - 1);
    }

    const bool ok = saveUserPresetsToDisk();
    syncPluginState(newType, newIndex, false);
    return ok;
}

bool PresetManager::renameCurrent(const std::string& newName)
{
    if (fCurrentType != PresetType::User)
        return false;
    DISTRHO_SAFE_ASSERT_RETURN(fCurrentIndex >= 0 && fCurrentIndex < (int)fUserPresets.size(), false)

    // No-op: preset already has this name.
    if (fUserPresets[fCurrentIndex].name == newName)
        return true;
    // Reject: another preset already uses this name.
    if (nameExists(newName))
        return false;

    fUserPresets[fCurrentIndex].name = newName;

    const bool ok = saveUserPresetsToDisk();
    syncPluginState(fModified);
    return ok;
}

// ── Import / Export ────────────────────────────────────────────────────────
bool PresetManager::hasImported() const
{
    return !fImportedPreset.name.empty();
}

const Preset* PresetManager::importedPreset() const
{
    return hasImported() ? &fImportedPreset : nullptr;
}

bool PresetManager::importFromFile(const std::string& filePath)
{
    try {
        std::ifstream f(filePath);
        if (!f.is_open()) return false;
        json j = json::parse(f);
        fImportedPreset.name         = j.value("name",          "Imported");
        fImportedPreset.roomSize     = j.value("room_size",     kParamRanges[PARAM_ROOM_SIZE].def);
        fImportedPreset.stereo       = j.value("stereo",        kParamRanges[PARAM_STEREO].def);
        fImportedPreset.damping      = j.value("damping",       kParamRanges[PARAM_DAMPING].def);
        fImportedPreset.hfColour     = j.value("hf_colour",     kParamRanges[PARAM_HF_COLOUR].def);
        fImportedPreset.earlyMix     = j.value("early_mix",     kParamRanges[PARAM_EARLY_MIX].def);
        fImportedPreset.wetDry       = j.value("wet_dry",       kParamRanges[PARAM_WET_DRY].def);
        fImportedPreset.outputVolume = j.value("output_volume", kParamRanges[PARAM_OUTPUT_VOLUME].def);
        fImportedPreset.preDelay     = j.value("pre_delay",     kParamRanges[PARAM_PREDELAY].def);
        selectImportedPreset();
        return true;
    } catch (...) {
        return false;
    }
}

bool PresetManager::exportCurrentToFile(const std::string& filePath)
{
    const Preset* p = currentPreset();
    if (!p) return false;
    try {
        json j;
        j["name"]          = p->name;
        j["room_size"]     = p->roomSize;
        j["stereo"]        = p->stereo;
        j["damping"]       = p->damping;
        j["hf_colour"]     = p->hfColour;
        j["early_mix"]     = p->earlyMix;
        j["wet_dry"]       = p->wetDry;
        j["output_volume"] = p->outputVolume;
        j["pre_delay"]     = p->preDelay;
        std::ofstream f(filePath);
        if (!f.is_open()) return false;
        f << j.dump(4);
        return true;
    } catch (...) {
        return false;
    }
}

bool PresetManager::commitImported(const std::string& name)
{
    if (!hasImported()) return false;
    if (nameExists(name)) return false;

    Preset p  = fImportedPreset;
    p.name    = name;
    fUserPresets.push_back(p);

    const bool ok = saveUserPresetsToDisk();
    syncPluginState(PresetType::User, (int)fUserPresets.size() - 1, false);
    return ok;
}

// ── Disk I/O ───────────────────────────────────────────────────────────────
bool PresetManager::loadUserPresetsFromDisk()
{
    fUserPresets.clear();
    const std::string path = _getUserPresetsFilePath();
    try {
        std::ifstream f(path);
        if (!f.is_open()) return true; // No file yet; not an error
        json j = json::parse(f);
        if (!j.is_array()) return false;
        for (const auto& item : j) {
            Preset p;
            p.name         = item.value("name",          "");
            p.roomSize     = item.value("room_size",     kParamRanges[PARAM_ROOM_SIZE].def);
            p.stereo       = item.value("stereo",        kParamRanges[PARAM_STEREO].def);
            p.damping      = item.value("damping",       kParamRanges[PARAM_DAMPING].def);
            p.hfColour     = item.value("hf_colour",     kParamRanges[PARAM_HF_COLOUR].def);
            p.earlyMix     = item.value("early_mix",     kParamRanges[PARAM_EARLY_MIX].def);
            p.wetDry       = item.value("wet_dry",       kParamRanges[PARAM_WET_DRY].def);
            p.outputVolume = item.value("output_volume", kParamRanges[PARAM_OUTPUT_VOLUME].def);
            p.preDelay     = item.value("pre_delay",     kParamRanges[PARAM_PREDELAY].def);
            if (!p.name.empty())
                fUserPresets.push_back(p);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool PresetManager::saveUserPresetsToDisk()
{
    if (!_ensureDataDirExists()) return false;
    const std::string path = _getUserPresetsFilePath();
    try {
        json j = json::array();
        for (const auto& p : fUserPresets) {
            json item;
            item["name"]          = p.name;
            item["room_size"]     = p.roomSize;
            item["stereo"]        = p.stereo;
            item["damping"]       = p.damping;
            item["hf_colour"]     = p.hfColour;
            item["early_mix"]     = p.earlyMix;
            item["wet_dry"]       = p.wetDry;
            item["output_volume"] = p.outputVolume;
            item["pre_delay"]     = p.preDelay;
            j.push_back(item);
        }
        std::ofstream f(path);
        if (!f.is_open()) return false;
        f << j.dump(4);
        return true;
    } catch (...) {
        return false;
    }
}

// ── State accessors ────────────────────────────────────────────────────────
const Preset* PresetManager::currentPreset() const
{
    switch (fCurrentType) {
    case PresetType::Factory:
        if (fCurrentIndex >= 0 && fCurrentIndex < kFactoryPresetsCount)
            return &kFactoryPresets[fCurrentIndex];
        break;
    case PresetType::User:
        if (fCurrentIndex >= 0 && fCurrentIndex < (int)fUserPresets.size())
            return &fUserPresets[fCurrentIndex];
        break;
    case PresetType::Imported:
        return &fImportedPreset;
    }
    return nullptr;
}

void PresetManager::markModified()
{
    if (!fModified)
        syncPluginState(true);
}

void PresetManager::clearModified()
{
    if (fModified)
        syncPluginState(false);
}

void PresetManager::syncPluginState(PresetType type, int index, bool modified)
{
    fCurrentType  = type;
    fCurrentIndex = index;
    syncPluginState(modified);
}

void PresetManager::syncPluginState(bool modified)
{
    fModified = modified;
    DISTRHO_SAFE_ASSERT_RETURN(fUI != nullptr, )

    const Preset* p      = currentPreset();
    const char*   name   = p ? p->name.c_str() : "";
    const char*   mod    = fModified ? "true" : "false";
    const char*   typeStr;

    switch (fCurrentType) {
    case PresetType::Factory:  typeStr = "Factory";  break;
    case PresetType::User:     typeStr = "User";     break;
    case PresetType::Imported: typeStr = "Imported"; break;
    default:                   typeStr = "Factory";  break;
    }

    fUI->setState(STATE_PRESET_NAME,     name);
    fUI->setState(STATE_PRESET_MODIFIED, mod);
    fUI->setState(STATE_PRESET_TYPE,     typeStr);
}

Preset PresetManager::snapshotFromUI() const
{
    Preset p;
    p.roomSize     = fUI->fParams[PARAM_ROOM_SIZE];
    p.stereo       = fUI->fParams[PARAM_STEREO];
    p.damping      = fUI->fParams[PARAM_DAMPING];
    p.hfColour     = fUI->fParams[PARAM_HF_COLOUR];
    p.earlyMix     = fUI->fParams[PARAM_EARLY_MIX];
    p.wetDry       = fUI->fParams[PARAM_WET_DRY];
    p.outputVolume = fUI->fParams[PARAM_OUTPUT_VOLUME];
    p.preDelay     = fUI->fParams[PARAM_PREDELAY];
    return p;
}

void PresetManager::restoreFromState(const std::string& typeStr,
                                     const std::string& nameStr,
                                     bool modified)
{
    // Restore type
    if (typeStr == "User")          fCurrentType = PresetType::User;
    else if (typeStr == "Imported") fCurrentType = PresetType::Imported;
    else                            fCurrentType = PresetType::Factory;

    // Restore index by searching for the name in the appropriate list.
    // Factory fallback is -1 ("Default") — not index 0 — so that an empty
    // preset_name on first launch resolves to Default, not "Grand Hall" (the first factory preset).
    fCurrentIndex = -1;
    if (fCurrentType == PresetType::Factory) {
        for (int i = 0; i < kFactoryPresetsCount; ++i) {
            if (kFactoryPresets[i].name == nameStr) { fCurrentIndex = i; break; }
        }
    } else if (fCurrentType == PresetType::User) {
        fCurrentIndex = -1;
        for (int i = 0; i < (int)fUserPresets.size(); ++i) {
            if (fUserPresets[i].name == nameStr) { fCurrentIndex = i; break; }
        }
        // If the user preset no longer exists (e.g. deleted after state was saved),
        // fall back to Factory / -1 (= Default) so the plugin is in a defined state.
        if (fCurrentIndex == -1) {
            fCurrentType  = PresetType::Factory;
            fModified     = false;
        }
    }
    // For Imported, index stays -1 (the name is in fImportedPreset which we can't restore)

    fModified = modified;
}

// ── Private: parameter application ────────────────────────────────────────
void PresetManager::_applyPreset(const Preset& preset)
{
    DISTRHO_SAFE_ASSERT_RETURN(fUI != nullptr, )
    _triggerParamUpdate(PARAM_ROOM_SIZE,     preset.roomSize);
    _triggerParamUpdate(PARAM_STEREO,        preset.stereo);
    _triggerParamUpdate(PARAM_DAMPING,       preset.damping);
    _triggerParamUpdate(PARAM_HF_COLOUR,     preset.hfColour);
    _triggerParamUpdate(PARAM_EARLY_MIX,     preset.earlyMix);
    _triggerParamUpdate(PARAM_WET_DRY,       preset.wetDry);
    _triggerParamUpdate(PARAM_OUTPUT_VOLUME, preset.outputVolume);
    _triggerParamUpdate(PARAM_PREDELAY,      preset.preDelay);
}

void PresetManager::_triggerParamUpdate(uint32_t index, float value)
{
    DISTRHO_SAFE_ASSERT_RETURN(index < PARAM_COUNT, )
    fUI->setParameterValue(index, value);
    fUI->parameterChanged(index, value);
}

// ── Private: platform-specific data directory ─────────────────────────────
std::string PresetManager::_getUserDataDir() const
{
#if defined(_WIN32)
    PWSTR wpath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &wpath))) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(len - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wpath, -1, &result[0], len, nullptr, nullptr);
        CoTaskMemFree(wpath);
        return result + "\\" CLASSIC_REVERB_APPDATA_DIR_NAME;
    }
    // Fallback
    const char* appdata = getenv("APPDATA");
    return std::string(appdata ? appdata : ".") + "\\" CLASSIC_REVERB_APPDATA_DIR_NAME;
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : nullptr;
    }
    return std::string(home ? home : ".") + "/Library/Application Support/" CLASSIC_REVERB_APPDATA_DIR_NAME;
#else
    // Linux / other POSIX
    const char* xdgData = getenv("XDG_DATA_HOME");
    if (xdgData && xdgData[0] != '\0')
        return std::string(xdgData) + "/" CLASSIC_REVERB_APPDATA_DIR_NAME;
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : nullptr;
    }
    return std::string(home ? home : ".") + "/.local/share/" CLASSIC_REVERB_APPDATA_DIR_NAME;
#endif
}

std::string PresetManager::_getUserPresetsFilePath() const
{
#if defined(_WIN32)
    return _getUserDataDir() + "\\" CLASSIC_REVERB_PRESET_FILE_NAME;
#else
    return _getUserDataDir() + "/" CLASSIC_REVERB_PRESET_FILE_NAME;
#endif
}

bool PresetManager::_ensureDataDirExists() const
{
    try {
        fs::create_directories(_getUserDataDir());
        return true;
    } catch (...) {
        return false;
    }
}
