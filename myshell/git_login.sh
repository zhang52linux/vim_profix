#!/bin/bash


#Author: szw_zhang
#Created Time: 2020-06-06
#Script Description: harddisk parttion script


git config --global user.name szw
git config --global user.email z52linux@gmail.com

cat ~/.gitconfig

git remote add origin https://github.com/zhang52linux/vim_profixies.git

git remote

git add *

git commit -m "my vim profixies first"

git reflog

git reset --hard "要回到的版本的哈希值"

git fetch origin master

git checkout origin/master

git checkout master

git merge origin/master #把远程代码合并到本地，相当于用远程代码跟新本地代码

git checkout origin/master

git merge master #把本地代码合并到远程，相当于用本地代码跟新远程代码

git merge origin master

git push origin master


