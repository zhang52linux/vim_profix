#!/bin/bash

git add *
git add .vimrc
git status
git commit -m "add linux.pptx"


while read username password 
do
/usr/bin/expect <<-EOF 
spawn git push origin master
set timeout 30
expect {
    "Username" { send "$username\r"; exp_continue }
    "Password" { send "$password\r" }
}
# interact
expect "$"
EOF
done < ~/passwd.txt






