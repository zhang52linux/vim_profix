#!/bin/bash


if [ ! -d "myshell" ];then
    mkdir myshell
    mv ./*.sh myshell
else
    mv ./*.sh myshell
fi
