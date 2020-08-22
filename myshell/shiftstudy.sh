#!/bin/bash

# description: shift使位置参数向左移动，默认移动一位，但可以shift 2 ...去改变


sum=0

while [ $# -ne 0 ]
do
    let sum=$sum+$1
    shift 2
done
echo $sum
