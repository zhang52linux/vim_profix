#!/bin/bash



read -p "Input a number:" num

if [ $num -eq 2 -o $num -eq 1 ];then
    echo "$num是质数!"
elif [ $[$num%2] -eq 0 -a $num -ne 2 ];then
    echo "$num不是质数!"
else
    for it in `seq 3 $num`
    do
        if [ $[$num % $it | bc] -eq 0 -a $num -eq $it ];then
            echo "$num是质数!"
            break
        fi
        if [ `expr $num % $it` -eq 0 -a $it -ne $num ];then
            echo "$num不是质数!"
            break
        fi
    done
fi
