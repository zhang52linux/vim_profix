#!/bin/bash

# name: szw
# data: 2020-08-18


if [ 1 -le 1 ];then
     echo 1
 elif [ 2 -eq 2 ];then
     echo 2
 else
     echo 3
fi


# 接收自己输入的参数
read -p "Input a word:" word


# 此处的$word 可以用来接收外界传递的参数
if [[ $word = "world" ]];then
    echo hello
else
    echo "world"
fi


echo "\$0=$0"
echo "\$#=$#"
echo "\$*=$*"
echo "\$@=$@"
echo "\$1=$1"
echo "\$2=$2"
echo "\$3=$3"
sudo rm -rf /tmp/*
# z
mkdir /tmp/dir{1..3}
cp /etc/hosts /tmp/dir1
echo "报告首长,任务已于$(date +%'F %T')完成"
