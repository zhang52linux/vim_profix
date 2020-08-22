#!/bin/bash


# uninstall 及配置文件
sudo apt autoremove --purge -y docker-ce docker-ce-cli containerd.io


#再清除配置文件-P
dpkg -l |grep ^rc|awk '{print $2}' |sudo xargs dpkg -P docker

#update cache
sudo apt update 

sudo apt upgrade -y
