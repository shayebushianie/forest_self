#include "storage/DatabaseManager.h"
#include <iostream>

DatabaseManager::DatabaseManager(const std::string& filePath)
    : filePath_(filePath)
{
}

DatabaseManager::~DatabaseManager()
{
    close();
}

bool DatabaseManager::open()
{
    file_.open(filePath_, std::ios::in | std::ios::out | std::ios::binary);

    if (!file_.is_open()) {
        // 文件不存在 → 先创建空文件
        std::fstream createFile(filePath_, std::ios::out | std::ios::binary);
        createFile.close();

        file_.open(filePath_, std::ios::in | std::ios::out | std::ios::binary);
        if (!file_.is_open()) {
            return false;
        }
        recordCount_ = 0;
    } else {
        // 文件已存在 → 计算记录数
        file_.seekg(0, std::ios::end);
        std::streampos fileSize = file_.tellg();
        recordCount_ = static_cast<uint32_t>(fileSize / RECORD_SIZE);
        file_.seekg(0, std::ios::beg);

        // 崩溃恢复检测
        recoverFromCrash();
    }

    return true;
}

void DatabaseManager::close()
{
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

uint32_t DatabaseManager::append(const FocusRecord& record)
{
    file_.clear();
    file_.seekp(0, std::ios::end);

    std::streampos fileSize = file_.tellp();
    uint32_t newId = static_cast<uint32_t>(fileSize / RECORD_SIZE);

    FocusRecord toWrite = record;
    toWrite.recordId = newId;

    file_.write(reinterpret_cast<const char*>(&toWrite), RECORD_SIZE);
    file_.flush();

    if (file_.good()) {
        ++recordCount_;
        return newId;
    }

    return 0;
}

std::optional<FocusRecord> DatabaseManager::readById(uint32_t recordId)
{
    if (recordId >= recordCount_) {
        return std::nullopt;
    }

    file_.clear();
    file_.seekg(static_cast<std::streamoff>(recordId) * RECORD_SIZE, std::ios::beg);

    FocusRecord record;
    file_.read(reinterpret_cast<char*>(&record), RECORD_SIZE);

    if (file_.fail()) {
        return std::nullopt;
    }

    return record;
}

bool DatabaseManager::updateById(uint32_t recordId, const FocusRecord& record)
{
    if (recordId >= recordCount_) {
        return false;
    }

    file_.clear();
    file_.seekp(static_cast<std::streamoff>(recordId) * RECORD_SIZE, std::ios::beg);
    file_.write(reinterpret_cast<const char*>(&record), RECORD_SIZE);
    file_.flush();

    return file_.good();
}

std::vector<FocusRecord> DatabaseManager::getAllRecords()
{
    std::vector<FocusRecord> records;
    records.reserve(recordCount_);

    file_.clear();
    file_.seekg(0, std::ios::beg);

    for (uint32_t i = 0; i < recordCount_; ++i) {
        FocusRecord record;
        file_.read(reinterpret_cast<char*>(&record), RECORD_SIZE);
        if (file_.fail()) {
            break;
        }
        records.push_back(record);
    }

    return records;
}

uint32_t DatabaseManager::count() const
{
    return recordCount_;
}

void DatabaseManager::recoverFromCrash()
{
    if (recordCount_ == 0) {
        return;
    }

    uint32_t lastIndex = recordCount_ - 1;
    auto lastRecord = readById(lastIndex);

    if (lastRecord.has_value() && lastRecord->status == 3) {
        // 上一次是非正常退出 → 自动标记为枯萎
        FocusRecord recovered = lastRecord.value();
        recovered.status = 1;           // 标记为失败/枯萎
        recovered.growthStage = 4;      // 枯萎阶段
        updateById(lastIndex, recovered);

        std::cout << "[CRASH RECOVERY] Record " << lastIndex
                  << " was running during last shutdown. Marked as withered.\n";
    }
}
