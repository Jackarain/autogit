# autogit

中文 | [English](README_EN.md)

**autogit** 是一个基于 C++20 协程的高性能 Git 仓库自动备份守护进程。它实时监控指定目录的文件变化，自动将更改提交并推送到远程 Git 服务器，实现无人值守的持续备份。

---

## 功能特性

### 🔄 自动提交与推送
- 自动检测仓库中的文件变更（新增、修改、删除、重命名、类型变更）
- 自动 `git add` 所有变更文件
- 自动创建提交并将更改推送到远程 Git 服务器
- 支持强制推送（force push）模式

### 👁️ 实时文件监控
- 基于操作系统的原生文件系统事件通知：
  - **Linux**: inotify
  - **macOS**: FSEvents
  - **Windows**: ReadDirectoryChangesW
- 无需轮询，即时响应文件变化
- 可配置的轮询回退间隔作为补充

### 🔐 多种认证方式
- **SSH 密钥认证**: 支持自定义公钥/私钥路径及密码短语
- **HTTP/HTTPS 认证**: 支持用户名/密码认证
- 自动适配远程仓库的认证方式

### 🏗️ 自动仓库初始化
- 如果目标目录尚未初始化 Git 仓库，自动创建并初始化
- 自动配置远程仓库地址
- 支持空仓库首次提交（Initial Commit）

### 📦 Git LFS 大文件存储
- **原生 LFS 指针文件支持**: 自动识别 `.gitattributes` 中配置为 `filter=lfs` 的文件模式
- **自动 Clean/Smudge 过滤**: 基于 libgit2 的 `git_filter` 机制，暂存时自动将大文件替换为指针文件，检出时自动还原
- **多方式 LFS 模式配置**: 支持通过 `.gitattributes` 和命令行 `--lfs_pattern` 参数指定 LFS 文件匹配模式（glob 通配符）
- **HTTP Batch API 推送**: 通过 LFS 批量 API 直接上传 LFS 对象到远程服务器
- **灵活的推送 URL**: 支持通过 `--lfs_push_url` 指定独立的 LFS 对象推送地址，默认从仓库远程 URL 推导

### ⚙️ 灵活配置
- 支持命令行参数和配置文件（`autogit.conf`）两种方式
- 自定义提交信息
- 自定义 Git 作者名称和邮箱
- 可配置的检查间隔
- 静默模式（关闭日志输出）
- 日志文件目录可配置

### 🐳 Docker 支持
- 提供 Alpine 和 Ubuntu 两种基础镜像的 Dockerfile
- 静态编译，镜像体积小
- 开箱即用的容器化部署

### 🖥️ 跨平台支持
- Linux、macOS、Windows 全平台兼容
- 使用 CMake 构建系统，支持 Ninja 加速编译
- 支持 GCC、Clang、MSVC 编译器
- 支持 ccache 编译缓存加速

---

## 快速开始

### 编译

```bash
git clone https://github.com/jackarain/autogit.git
cd autogit
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -G Ninja
ninja
```

编译完成后，可执行文件位于 `build/bin/autogit`。

### 基本用法

监控一个已存在的 Git 仓库并自动备份：

```bash
autogit --repository /path/to/your/repo \
        --git_remote_url https://github.com/user/repo.git \
        --git_author "Your Name" \
        --git_email "your@email.com"
```

### 配置 SSH 认证

```bash
autogit --repository /path/to/your/repo \
        --git_remote_url git@github.com:user/repo.git \
        --ssh_privkey /path/to/id_rsa \
        --ssh_pubkey /path/to/id_rsa.pub
```

### 作为 systemd 服务运行

```ini
[Unit]
Description=Auto Git Backup Daemon
After=network.target

[Service]
ExecStart=/usr/local/bin/autogit --repository /path/to/repo --check_interval 60
WorkingDirectory=/tmp/
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

### Docker 部署

```bash
docker build -t autogit -f Dockerfile .
docker run -d \
    -v /path/to/repo:/data \
    autogit --repository /data --quiet true
```

---

## 命令行选项

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `--repository` | — | 要监控的 Git 仓库路径 |
| `--config` | `autogit.conf` | 配置文件路径 |
| `--check_interval` | `60` | 检查间隔（秒） |
| `--commit_msg` | `Commit by autogit` | 自定义提交信息 |
| `--force_push` | `false` | 启用强制推送 |
| `--git_author` | — | Git 提交作者名称 |
| `--git_email` | — | Git 提交作者邮箱 |
| `--git_remote_url` | — | 远程仓库 URL |
| `--http_username` | — | HTTP 认证用户名 |
| `--http_password` | — | HTTP 认证密码 |
| `--ssh_pubkey` | — | SSH 公钥路径 |
| `--ssh_privkey` | — | SSH 私钥路径 |
| `--ssh_passphrase` | — | SSH 密钥密码短语 |
| `--quiet` | `false` | 静默模式 |
| `--log_dir` | — | 日志文件目录 |
| `--lfs` | `false` | 启用 Git LFS 支持，匹配 `.gitattributes` 中 LFS 模式的文件将以指针文件存储 |
| `--lfs_pattern` | — | 额外的 LFS 文件匹配模式（glob），例如 `--lfs_pattern '*.psd' --lfs_pattern '*.zip'` |
| `--lfs_push_url` | — | LFS 对象推送 URL，覆盖 `.lfsconfig` 中的设置；为空时使用仓库远程 origin URL |

---

## 技术架构

autogit 基于以下核心技术构建：

- **C++20 协程** — 使用 Boost.Asio 的 `awaitable` 实现异步 I/O 和协程化控制流
- **libgit2** — 纯 C 实现的 Git 核心操作库，无需安装 Git 命令行工具
- **Boost 库栈** — Asio（网络 & 异步）、Filesystem（文件系统）、Program Options（参数解析）
- **watchman 模块** — 跨平台文件系统事件监控抽象层
- **gitpp** — 基于 libgit2 的现代 C++ RAII 封装库（位于 `incubator/gitpp/`）
- **httpc** — 基于 Boost.Beast 和 Boost.Asio 的 C++20 协程 HTTP 客户端库（位于 `incubator/httpc/`），支持 SSL、连接复用和流式请求/响应

---

## 许可证

本项目基于 [Boost Software License 1.0](LICENSE) 开源。
