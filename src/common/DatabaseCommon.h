#ifndef DATABASECOMMON_H
#define DATABASECOMMON_H

#include <cstdint>
#include <cstring>

/**
 * @brief 专注会话记录 - 定长 64 字节
 *
 * 设计原则：
 *   1. 严禁包含任何指针、std::string 等变长成员
 *   2. 所有字段均为定长基础类型或定长 char 数组
 *   3. 使用 #pragma pack(push, 1) 消除编译器对齐填充
 *   4. static_assert 编译期校验结构体大小
 */
#pragma pack(push, 1)
struct FocusRecord {
    uint32_t recordId;        // 0-3   记录 ID（主键，自增）
    uint32_t plantType;       // 4-7   植物类型：0=Oak, 1=Pine, 2=Rose
    uint32_t plannedMinutes;  // 8-11  计划专注时长（分钟）
    uint32_t actualSeconds;   // 12-15 实际专注秒数
    uint64_t startTimestamp;  // 16-23 开始时间戳（Unix epoch）
    uint32_t status;          // 24-27 状态：0=成功, 1=失败(违规), 2=中途放弃, 3=正在专注(RUNNING)
    uint32_t violationCount;  // 28-31 违规次数
    uint32_t growthStage;     // 32-35 最终生长阶段
    uint32_t coinsEarned;     // 36-39 本次获得金币数
    uint32_t tagId;           // 40-43 标签：0=无, 1=学习, 2=写代码, 3=阅读, 4=运动
    uint8_t  focusMode;       // 44    专注模式：0=严格, 1=温和
    char     reserved[19];    // 45-63 对齐填充，确保总计 64 字节

    /// @brief 默认构造函数：清零所有字段，防止脏数据写入磁盘
    FocusRecord()
    {
        std::memset(this, 0, sizeof(FocusRecord));
    }
};
#pragma pack(pop)

// 编译期强制校验结构体大小
static_assert(sizeof(FocusRecord) == 64,
              "FocusRecord must be exactly 64 bytes for fixed-length random access");

// ---------- 快捷预设结构体 (64 字节) ----------

#pragma pack(push, 1)
struct FocusPreset {
    uint32_t presetId;
    char     presetName[16];
    uint32_t plantType;
    uint32_t plannedMinutes;
    uint32_t tagId;
    uint32_t soundId;
    char     reserved[28];

    FocusPreset() { std::memset(this, 0, sizeof(FocusPreset)); }

    void pack(char buffer[64]) const {
        std::memcpy(buffer, this, 64);
    }

    void unpack(const char buffer[64]) {
        std::memcpy(this, buffer, 64);
    }
};
#pragma pack(pop)

static_assert(sizeof(FocusPreset) == 64,
              "FocusPreset must be exactly 64 bytes");

// ---------- 用户账号结构体 (64 字节) ----------

#pragma pack(push, 1)
struct UserRecord {
    uint32_t userId;
    char     username[24];
    char     passwordHash[32];
    char     reserved[4];

    UserRecord() { std::memset(this, 0, sizeof(UserRecord)); }

    void pack(char buffer[64]) const {
        std::memcpy(buffer, this, 64);
    }

    void unpack(const char buffer[64]) {
        std::memcpy(this, buffer, 64);
    }
};
#pragma pack(pop)

static_assert(sizeof(UserRecord) == 64,
              "UserRecord must be exactly 64 bytes");

#endif // DATABASECOMMON_H
