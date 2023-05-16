# autogit
中文 | [English](README_EN.md)
<br>

#### 这是一个自动化的备份目录的程序，其功能主要完成针对一个 Git 仓库目录，自动提交并推送到 Git 备份服务器。

<br>

本程序主要为解决本人在 Linux 下在一些复杂操作流程时难以记忆的问题（比如配置一些复杂的软件如 gitlab），它可以利用 asciinema 自动记录所有 shell 操作并将自动 Push 到一个远程 Git 仓库用以备份，这不仅够方便地追溯和复现每一步操作，也便于将这些操作记录通过 Git 仓库进行保存和分享。

<br>
<br>

这是我个人的使用方法示例参考：

1. 创建 /root/.cache/asciinema/ 目录用于作为 git 仓库。
2. 配置好上面步骤1中创建的 git 仓库推送地址等信息。
3. 安装 asciinema，然后配置 .zshrc， 以便于打开 shell 时自动开始录制所有 shell 操作，在这个文件尾部添加以下代码：
```
ctime=$(date +%Y%m%d_%H%M%S)

if [ -z "$recsession" ]; then
    export recsession=$$
    echo "Current time: $ctime, recsession: $recsession"
    /usr/bin/asciinema rec "/root/.cache/asciinema/$ctime-$recsession-ascii.cast"
fi
```
完成后保存，这之后在打开 shell 时将会自动运行 asciinema，并将你在 shell 中的所有操作录制并自动保存到 /root/.cache/asciinema/ 这个 git 仓库所在的目录中。

4. 编译本项目，成功得将得到可执行程序 autogit，将其复制到 /usr/local/bin/autogit，然后制作一个 systemd 服务用于自动运行 autogit：
```
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

然后启动这个服务，从这之后，所有在 shell 中的操作将被录制。
