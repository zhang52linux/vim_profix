#!/bin/bash


read -p "Input a process's name:" pname

pgrep $pname &> /dev/null

if [ $? -eq 0 ];then
    echo "yes,it exists!,pid is $(pgrep $pname),status is $(systemctl status ssh | grep "Active" | tr -s " "| cut -d' ' -f4 | cut -d'(' -f2 | cut -d')' -f1)"
else
    echo "sorry,it is empty!"
fi
