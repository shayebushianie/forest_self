#ifndef PRESETMANAGER_H
#define PRESETMANAGER_H

#include <vector>
#include <string>
#include "common/DatabaseCommon.h"

class PresetManager {
public:
    explicit PresetManager(const std::string& filePath);
    ~PresetManager();

    bool loadPresets();
    bool savePreset(uint32_t index, const FocusPreset& preset);
    const std::vector<FocusPreset>& getPresets() const { return presets_; }

private:
    void writeDefaultPresets();

    std::string filePath_;
    std::vector<FocusPreset> presets_;
    static constexpr size_t RECORD_SIZE = 64;
};

#endif // PRESETMANAGER_H
