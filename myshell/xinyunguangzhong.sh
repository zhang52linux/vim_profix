#!/bin/bash

phone=./iphone.txt

if [ -f "./luck.txt" ];then
    sudo rm -f ./luck.txt
fi

for((i=1;i<=5;++i))
do
    line=`wc -l $phone | cut -d' ' -f1`
    luck_line=$[$RANDOM%$line+1]
    line_num=`head -$luck_line $phone | tail -1`
    luck="139****${line_num:7:4}"
    echo $luck
    echo $line_num >> luck.txt
    sed -i "/$line_num/d" $phone
done










