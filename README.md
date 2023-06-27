# autogit

中文 | [English](README_EN.md)
\
\
这是一个自动化备份 `Git` 仓库目录的程序，其主要功能是针对一个 `Git` 仓库目录，自动添加目录下所有文件提交并推送到 `Git` 服务器。
\
\
开发本程序的主要动机是为了解决本人在 `Linux` 下进行一些复杂操作时难以记忆的问题（例如配置一些复杂的软件如 `GitLab`、`Nextcloud` 等等）。利用 `asciinema` 自动记录所有 `shell` 操作，并将记录文件自动推送到一个远程 `Git` 仓库用于备份。这不仅可以方便地追溯和复现每一步操作，也可以通过 `Git` 仓库轻松保存和分享这些操作记录。
\
\
当然本程序也可以用于其它方面的文件备份，例如用于自动备份文档、配置文件等等。
\
\
这是我个人的使用方法示例：

1. 创建 `/root/.cache/asciinema/` 目录作为 `git` 仓库。
2. 配置上一步创建的 `Git` 仓库的推送地址等信息。
3. 安装 `asciinema`，然后配置 `.zshrc`， 以便于在打开 `shell` 时自动开始录制所有 `shell` 上的操作，在这个文件尾部添加以下代码：

```bash
ctime=$(date +%Y%m%d_%H%M%S)

if [ -z "$recsession" ]; then
    export recsession=$$
    echo "Current time: $ctime, recsession: $recsession"
    /usr/bin/asciinema rec "/root/.cache/asciinema/$ctime-$recsession-ascii.cast"
fi
```

修改完成后保存，在这之后在打开 `shell` 时将会自动运行 `asciinema`，并将你在 `shell` 中的所有操作录制并自动保存到 `/root/.cache/asciinema/` 这个 `Git` 仓库目录中。

4. 编译本项目，成功将得到可执行程序 `autogit`，将其复制到 `/usr/local/bin/autogit`，然后创建一个 `systemd` 服务用于自动运行 `autogit`：

```bash
[Unit]
Description=Git watch service
After=network.target

[Service]
User=root
ExecStart=/usr/local/bin/autogit --quiet true --check_interval 60 --repository /root/.cache/asciinema
WorkingDirectory=/tmp/
ExecReload=/bin/kill -HUP $MAINPID
KillMode=process
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

然后启动这个服务，从这之后，所有在 `shell` 中的操作将被录制。
