# autogit

#### This is an automated program for backing up directories. Its main function is to automatically commit and push a Git repository directory to a Git backup server.

<br>

The primary motivation behind developing this program is to address the problem of struggling to remember complex operations in Linux (for example, configuring complex software like GitLab, Nextcloud, and so forth). With the help of asciinema, all shell operations are automatically recorded, and the record files are then automatically pushed to a remote Git repository for backup. This makes it easy not only to trace and reproduce every step of an operation but also to save and share these operation records through the Git repository.

Of course, this program can also be used for other file backup purposes, such as automating the backup of documents, configuration files, etc.
<br>
<br>

Here is a reference for usage:

1. Create /root/.cache/asciinema/ directory as a Git repository.
2. Configure the push address and other information for the Git repository created in the previous step.
3. Install asciinema, then configure .zshrc to automatically start recording all operations on the shell upon opening. Add the following code at the end of this file:

```
ctime=$(date +%Y%m%d_%H%M%S)

if [ -z "$recsession" ]; then
    export recsession=$$
    echo "Current time: $ctime, recsession: $recsession"
    /usr/bin/asciinema rec "/root/.cache/asciinema/$ctime-$recsession-ascii.cast"
fi
```

Save after making these changes. From then on, asciinema will run automatically when opening the shell, recording all your shell operations and saving them to the /root/.cache/asciinema/ Git repository directory.

4. Compile this project to obtain the executable program autogit. Copy it to /usr/local/bin/autogit, and then create a systemd service to automatically run autogit:
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

Then start this service. From this point on, all operations on the shell will be recorded.
