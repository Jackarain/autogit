# autogit

#### This is an automated program for backing up directories. Its main function is to automatically commit and push a Git repository directory to a Git backup server.

<br>

This program solves the problem for Linux users who find it difficult to remember some complex operation procedures (such as configuring complex software like GitLab). It can use asciinema to automatically record all your shell operations and automatically push them to a remote Git repository for backup. This not only allows you to conveniently trace and reproduce each operation, but also makes it easy for you to save and share these operation records through the Git repository.

<br>
<br>

Here is a reference for usage:

1. Create the /root/.cache/asciinema/ directory to be used as a git repository.
2. Configure the push address and other information for the git repository created in step 1.
3. Install asciinema, then configure .zshrc so that when you open the shell, all your shell operations will automatically start recording. Add the following code to the end of this file:

```
ctime=$(date +%Y%m%d_%H%M%S)

if [ -z "$recsession" ]; then
    export recsession=$$
    echo "Current time: $ctime, recsession: $recsession"
    /usr/bin/asciinema rec "/root/.cache/asciinema/$ctime-$recsession-ascii.cast"
fi
```

Save after completion. From now on, asciinema will run automatically when you open the shell, and all your operations in the shell will be recorded and automatically saved to the directory where the /root/.cache/asciinema/ git repository is located.

4. Compile this project to get the executable program autogit. Copy it to /usr/local/bin/autogit, then create a systemd service to automatically run autogit:
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

Then start this service. From then on, all operations in the shell will be recorded.
