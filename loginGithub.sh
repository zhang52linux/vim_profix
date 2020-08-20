#!/bin/bash

#用户自定义写法
# set username [ lindex &argv 0 ]
# set password [ lindex &argv 1 ] 
 
# 固定写法
# set username zhang52linux
# set password linux842563
set timeout 10                   # 设置超时时间

while read username password 
do
    /usr/bin/expect <<-END &> /dev/null
    spawn git push origin master
    expect {
         "Username" { send "$username\r";exp_continue }
         "Password" { send "$password\r" }
    }
    END

done < passwd.txt

# interact
