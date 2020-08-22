#!/usr/bin/expect

#开启一个程序
spawn ssh z52linux@192.168.152.136


##捕获相关内容
expect {
       "(yes/no)?" { send "yes\r";exp_continue }
       "password:" { send "z\r" }
}

#交互
interact 


##脚本执行方式
# chmod a+x #0
# ./#0.sh
