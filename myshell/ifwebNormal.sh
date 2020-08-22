#!/bin/bash


read -p "Please input a website:" website

# curl $website &> /dev/null
elinks $website &> /dev/null
# wget $website -P ~/文档


if [ $? -eq 0 ];then
    echo "ok" && rm -rf ~/文档/index.*
else
    echo "error"
fi


