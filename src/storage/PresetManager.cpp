#include "storage/PresetManager.h"
#include <fstream>
#include <cstring>
#include <iostream>

PresetManager::PresetManager(const std::string& filePath) : filePath_(filePath)
{
    presets_.resize(3);
}

PresetManager::~PresetManager() {}

void PresetManager::writeDefaultPresets()
{
    FocusPreset p0;
    p0.presetId = 0;
    std::strncpy(p0.presetName, "Code Rush", 15);
    p0.plantType = 0;
    p0.plannedMinutes = 30;
    p0.tagId = 2;
    p0.soundId = 3;

    FocusPreset p1;
    p1.presetId = 1;
    std::strncpy(p1.presetName, "Deep Read", 15);
    p1.plantType = 1;
    p1.plannedMinutes = 45;
    p1.tagId = 3;
    p1.soundId = 2;

    FocusPreset p2;
    p2.presetId = 2;
    std::strncpy(p2.presetName, "Free Study", 15);
    p2.plantType = 2;
    p2.plannedMinutes = 0;
    p2.tagId = 1;
    p2.soundId = 1;

    presets_[0] = p0;
    presets_[1] = p1;
    presets_[2] = p2;

    std::ofstream out(filePath_, std::ios::binary | std::ios::trunc);
    if (out.is_open()) {
        for (const auto& p : presets_) {
            char buf[RECORD_SIZE];
            p.pack(buf);
            out.write(buf, RECORD_SIZE);
        }
        out.close();
    }
}

bool PresetManager::loadPresets()
{
    std::ifstream in(filePath_, std::ios::binary);
    if (!in.is_open()) {
        writeDefaultPresets();
        return true;
    }

    for (size_t i = 0; i < 3; ++i) {
        char buf[RECORD_SIZE];
        in.read(buf, RECORD_SIZE);
        if (in.gcount() == RECORD_SIZE) {
            presets_[i].unpack(buf);
        } else {
            in.close();
            writeDefaultPresets();
            return true;
        }
    }
    in.close();
    return true;
}

bool PresetManager::savePreset(uint32_t index, const FocusPreset& preset)
{
    if (index >= 3) return false;
    presets_[index] = preset;

    std::fstream out(filePath_, std::ios::binary | std::ios::in | std::ios::out);
    if (out.is_open()) {
        out.seekp(index * RECORD_SIZE, std::ios::beg);
        char buf[RECORD_SIZE];
        preset.pack(buf);
        out.write(buf, RECORD_SIZE);
        out.close();
        return true;
    }
    return false;
}
