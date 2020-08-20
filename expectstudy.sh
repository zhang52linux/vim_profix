#!/bin/bash

git add *
git status
sleep 3
git commit -m "add linux.pptx"

/usr/bin/expect <<-EOF 
spawn git push origin master
set username [ lindex $argv 0 ]
set password [ lindex $argv 1 ]
expect {
    "Username" { send "$username\r"; exp_continue }
    "Password" { send "$password\r" }
}
interact
EOF









