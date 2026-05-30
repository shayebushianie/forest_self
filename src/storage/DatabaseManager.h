#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include "common/DatabaseCommon.h"
#include <fstream>
#include <string>
#include <optional>
#include <vector>

/**
 * @brief 定长二进制文件存储管理器
 *
 * 以 64 字节为单位的定长记录存储。
 * 通过 recordId 计算偏移量实现随机读写 (seekp/seekg)。
 * 支持原地更新单条记录。
 *
 * 严禁引用任何第三方数据库库，严禁使用 JSON/XML 等文本序列化。
 */
class DatabaseManager {
public:
    /// @param filePath 数据文件路径
    /// @note 调用方应使用 QCoreApplication::applicationDirPath() 动态获取 exe 所在目录，
    ///       拼接文件名传入，严禁硬编码绝对路径
    explicit DatabaseManager(const std::string& filePath);
    ~DatabaseManager();

    /// 打开数据文件（不存在则自动创建；若存在且最后一条记录 status==3，自动标记为枯萎）
    bool open();

    /// 关闭文件并刷新缓冲区
    void close();

    /// 追加一条新记录（自动分配 recordId = fileSize / 64），返回新记录 ID
    uint32_t append(const FocusRecord& record);

    /// 按 recordId 随机读取（偏移 = recordId * 64）
    std::optional<FocusRecord> readById(uint32_t recordId);

    /// 按 recordId 原地更新（偏移 = recordId * 64）
    bool updateById(uint32_t recordId, const FocusRecord& record);

    /// 返回文件中所有记录（供 GardenCanvas 加载历史）
    std::vector<FocusRecord> getAllRecords();

    /// 返回文件中记录总数
    uint32_t count() const;

private:
    /// 启动时检测并修复崩溃恢复（最后一条记录 status==3 → 改为 1）
    void recoverFromCrash();

    std::string   filePath_;
    std::fstream  file_;
    uint32_t      recordCount_ = 0;

    static constexpr size_t RECORD_SIZE = 64;
};

#endif // DATABASEMANAGER_H
