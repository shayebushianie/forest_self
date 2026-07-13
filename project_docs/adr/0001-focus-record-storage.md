# ADR 0001: 专注记录使用 SQLite

- 决策：专注历史以 `sessions.sqlite` 为唯一事实来源；旧 `sessions.dat` 在首次启动时备份并事务导入。
- 原因：检查点和终态更新不能继续重写全部历史记录。
- 约束：钱包、用户和功能小状态继续使用 `StorageEnvelope`，不混入 SQLite。
