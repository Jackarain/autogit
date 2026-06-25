# autogit

[中文](README.md) | English

**autogit** is a high-performance Git repository auto-backup daemon built with C++20 coroutines. It monitors file changes in a specified directory in real time, automatically commits and pushes changes to a remote Git server, providing unattended continuous backup.

---

## Features

### 🔄 Automatic Commit & Push
- Automatically detects file changes (new, modified, deleted, renamed, type changed)
- Automatically stages all changed files (`git add`)
- Automatically creates commits and pushes to remote Git server
- Supports force push mode

### 👁️ Real-time File Watching
- Native file system event notifications per platform:
  - **Linux**: inotify
  - **macOS**: FSEvents
  - **Windows**: ReadDirectoryChangesW
- Instant response to file changes without polling
- Configurable polling fallback interval

### 🔐 Multiple Authentication Methods
- **SSH key authentication**: Custom public/private key paths and passphrase
- **HTTP/HTTPS authentication**: Username/password support
- Automatically adapts to remote repository authentication

### 🏗️ Automatic Repository Initialization
- Auto-initializes Git repository if target directory is not yet a repo
- Automatically configures remote URL
- Supports initial commit for empty repositories

### 📦 Git LFS Support
- **Native LFS Pointer Support**: Automatically detects patterns configured as `filter=lfs` in `.gitattributes`
- **Automatic Clean/Smudge Filtering**: Uses libgit2's `git_filter` mechanism — automatically replaces large files with pointer files on staging and restores them on checkout
- **Flexible Pattern Configuration**: Supports LFS file patterns via `.gitattributes` and command-line `--lfs_pattern` (glob wildcards)
- **HTTP Batch API Upload**: Pushes LFS objects directly to remote servers via LFS Batch API
- **Configurable Push URL**: Supports a dedicated LFS push URL via `--lfs_push_url`, defaults to the repository remote origin URL

### ⚙️ Flexible Configuration
- Command-line arguments and configuration file (`autogit.conf`) support
- Custom commit messages
- Custom Git author name and email
- Configurable check interval
- Quiet mode (disable logging)
- Configurable log directory

### 🐳 Docker Support
- Dockerfiles for both Alpine and Ubuntu bases
- Static linking for small image size
- Ready-to-use containerized deployment

### 🖥️ Cross-platform
- Linux, macOS, Windows — fully compatible
- CMake build system with Ninja support
- GCC, Clang, MSVC compiler support
- ccache build cache support

---

## Quick Start

### Build

```bash
git clone https://github.com/jackarain/autogit.git
cd autogit
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -G Ninja
ninja
```

The executable will be at `build/bin/autogit`.

### Basic Usage

Monitor an existing Git repository and back it up automatically:

```bash
autogit --repository /path/to/your/repo \
        --git_remote_url https://github.com/user/repo.git \
        --git_author "Your Name" \
        --git_email "your@email.com"
```

### SSH Authentication

```bash
autogit --repository /path/to/your/repo \
        --git_remote_url git@github.com:user/repo.git \
        --ssh_privkey /path/to/id_rsa \
        --ssh_pubkey /path/to/id_rsa.pub
```

### systemd Service

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

### Docker Deployment

```bash
docker build -t autogit -f Dockerfile .
docker run -d \
    -v /path/to/repo:/data \
    autogit --repository /data --quiet true
```

---

## Command-line Options

| Option | Default | Description |
|--------|---------|-------------|
| `--repository` | — | Path to the Git repository to watch |
| `--config` | `autogit.conf` | Path to configuration file |
| `--check_interval` | `60` | Check interval in seconds |
| `--commit_msg` | `Commit by autogit` | Custom commit message |
| `--force_push` | `false` | Enable force push |
| `--git_author` | — | Git commit author name |
| `--git_email` | — | Git commit author email |
| `--git_remote_url` | — | Remote repository URL |
| `--http_username` | — | HTTP auth username |
| `--http_password` | — | HTTP auth password |
| `--ssh_pubkey` | — | SSH public key path |
| `--ssh_privkey` | — | SSH private key path |
| `--ssh_passphrase` | — | SSH key passphrase |
| `--quiet` | `false` | Quiet mode (suppress logging) |
| `--log_dir` | — | Log file directory |
| `--lfs` | `false` | Enable Git LFS support; files matching LFS patterns in `.gitattributes` will be stored as pointers |
| `--lfs_pattern` | — | Additional LFS file patterns (glob), e.g. `--lfs_pattern '*.psd' --lfs_pattern '*.zip'` |
| `--lfs_push_url` | — | LFS remote URL for pushing objects; overrides `.lfsconfig`; falls back to repository remote origin URL |

---

## Technical Architecture

autogit is built on the following core technologies:

- **C++20 Coroutines** — Boost.Asio `awaitable`-based asynchronous I/O and coroutine control flow
- **libgit2** — Pure C Git core operations library, no Git CLI dependency
- **Boost Libraries** — Asio (networking & async), Filesystem (file system), Program Options (argument parsing)
- **watchman Module** — Cross-platform file system event monitoring abstraction layer
- **gitpp** — Modern C++ RAII wrapper around libgit2 (located in `incubator/gitpp/`)
- **httpc** — C++20 coroutine-based HTTP client library built on Boost.Beast and Boost.Asio (located in `incubator/httpc/`), with SSL support, connection reuse, and streaming request/response

---

## License

This project is open source under the [Boost Software License 1.0](LICENSE).
