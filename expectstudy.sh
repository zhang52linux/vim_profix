#!/bin/bash

git add *
git commit -m "add linux.pptx"



/usr/bin/expect <<-EOF 
if {$argc < 2} {
    send_user "Usage:\n  $argv0 IPaddr User Passwd Port Passphrase\n"
    puts stderr "argv error!\n"
    sleep 1
    exit 1
}
spawn git push origin master
set username [ lindex $argv 0 ]
set password [ lindex $argv 1 ]
expect {
"Username" { send "$username\r"; exp_continue }
"Password" { send "$password\r" }
}
interact
EOF









