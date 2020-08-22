#!/bin/bash


# export NVIM_PORT="$HOME/.nvm"

# if [ 3 -eq 1 ];then
#     echo "hello"
# elif [ 2 -eq 3 ];then
#     echo "world"
# elif [ -s "$NVIM_PORT/nvm.sh" ];then
#     echo "world hello"
# else
#     echo "hello world"
# fi


# read -p "Input your IP:" IP

# ping -c1 $IP &>/dev/null

if [ $? -eq 0 ];then
    echo $(ifconfig ens33 | grep "netmask" | tr -s ' '|cut -d' ' -f3)
else
    echo "error"
fi
