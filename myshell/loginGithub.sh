#!/usr/bin/expect

spawn git push origin master
set username [ lindex $argv 0 ]
set password [ lindex $argv 1 ]
expect {
    "Username" { send "$username\r"; exp_continue }
    "Password" { send "$password\r" }
}
interact





















