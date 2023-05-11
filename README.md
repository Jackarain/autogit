# autogit

##### 一个自动 git 提交并 push 的备份目录服务，本项目主要用于解决本人在使用 linux 时，经常进行一些较为复杂的操作流程时（比如搭建 gitlab、配置 nextcloud 等复杂项目）便于使用 asciinema 自动记录整个流程，并自动提交到 git 仓库。

<br>
<br>

使用方法参考：

1. 创建 ~/.cache/asciinema 用于作为 git 仓库。
2. 配置好仓库推送地址等，推送无需用户输入验证等便携于自动 git push。
3. 在安装 asciinema后，配置 .zshrc 以便于打开 shell 时自动开始录制，在该文件尾部添加代码：
```
ctime=$(date +%Y%m%d_%H%M%S)

if [ -z "$recsession" ]; then
    export recsession=$$
    echo "Current time: $ctime, recsession: $recsession"
    /usr/bin/asciinema rec "/root/.cache/asciinema/$ctime-$recsession-ascii.cast"
fi
```
这样，则在打开 shell 时自动运行 asciinema，并将屏幕录制自动保存到 /root/.cache/asciinema/ 这个 git 仓库目录中。

4. 将 autogit 复制到 /usr/local/bin/autogit，然后添加 autogit 的 systemd 服务：
```
[Unit]
Description=Git watch service
After=network.target

[Service]
User=root
ExecStart=/usr/local/bin/autogit --disable_log true --time 60 --repository /root/.cache/asciinema
WorkingDirectory=/tmp/
ExecReload=/bin/kill -HUP $MAINPID
KillMode=process
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

然后启动服务即可。

