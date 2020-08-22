#!/bin/bash

# seq {} 1 2 3 4

# for i in 1 2 3 4
# # for i in $(seq 10)
# for i in {10..1}
# do
#     echo "hello world"
#     echo $i
# done

# 不带列表的循环语句，i由用户自定义
# for i
# do
#     echo "ssss"
# done


# 类c风格的for,打印偶数
sum=0
cur=0
for((i=1;i<=100;i++))
do
    sum=$(expr $i % 2)
    cur=$i
    if [ $sum -eq 0 ];then
        echo $i
        break
    fi
done

echo "当前值为:$cur"


# 计算基数之和
sum=0
for((i=1;i<=100;++i))
do
    if [ $[$i%2] -ne 0 ];then
        let sum=$sum+$i
    fi
done

echo "1-100的奇数和为: $sum"

# 计算偶数之和
sum=0
for((i=1;i<=100;++i))
do
    if [ $[$i%2] -eq 0 ];then
        let sum=$sum+$i
    fi
done

echo "1-100的奇数为:$sum"


for i in {1,2,3}
do
    sudo rm -rf /tmp/dir$i
done







