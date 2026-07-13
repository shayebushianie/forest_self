# ADR 0002: 运行数据目录

- 安装版数据位于当前用户的 `AppLocalDataLocation`。
- 仅可执行文件同级存在 `forest.portable` 标识时使用同级 `app_data`。
- 首次启动会复制旧安装目录的数据；已有用户目录中的文件优先。
