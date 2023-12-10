#
# 终端自动录制操作并存储于 git 仓库
#
# autogit.sh 用于 .zshrc 或 .bashrc 中，用于自动记录终端会话中的所有操作记录
#
# 在 .zshrc 或 .bashrc 中，添加如下内容：
# source ~/.autogit/autogit.sh
#
# 由于 autogit.sh 依赖于 asciinema, 所以在使用 autogit.sh 之前，需要先安装 asciinema
#
# 在 Mac OS X 上，可以使用 Homebrew 安装 asciinema
# brew install asciinema
#
# 在 Linux 上，可以使用 pacman 或 apt-get 或 yum 安装 asciinema
# pacman -S asciinema
#
# 在 Windows 上，可以使用 scoop 安装 asciinema
# scoop install asciinema
#
# 在安装好 asciinema 之后，先检查一下是否能正常运行 asciinema
#
# 系统中是否存在 python，如果不存在，需要先安装 python
# 如果系统中已经存在 python，可以跳过这一步
# 然后再检查是否存在 asciinema.sh 脚本是否存在, 如果不存在, 则可以创建
# /usr/bin/asciinema.sh 脚本，内容如下：
#    ```
#    #!/bin/sh
#    /usr/bin/python -m asciinema "$@"
#    ```

if [ "$VSCODE_RESOLVING_ENVIRONMENT" != "1" ]; then
    ctime=$(date +%Y%m%d_%H%M%S)
    cyear=$(date +%Y)
    cmonth=$(date +%m)
    workdir=$HOME

    if [ -z "$recsession" ]; then
        mkdir -p "$workdir/.cache/asciinema/$cyear/$cmonth"
        export recsession=$$
        echo "Current time: $ctime, recsession: $recsession"
        asciinema.sh rec "$workdir/.cache/asciinema/$cyear/$cmonth/$ctime-$recsession-ascii.cast"
    fi
fi
