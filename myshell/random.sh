#!/bin/bash

#description： random随机产生0-323767随机数

# 产生0-2之间的随机数
# echo $[$RANDOM%3]

# 产生0-3之间的随机数
# echo $[$RANDOM%4]

# 产生0-100之间的随机数机数
# echo $[$RANDOM%101]

# 产生1-100之间的随机数
# echo $[$RANDOM%101+1]


for ((i=1;i<=1000;++i))
do
    n1=$[$RANDOM%10]
    n2=$[$RANDOM%10]
    n3=$[$RANDOM%10]
    n4=$[$RANDOM%10]
    n5=$[$RANDOM%10]
    n6=$[$RANDOM%10]
    n7=$[$RANDOM%10]
    n8=$[$RANDOM%10]
    echo "139$n1$n2$n3$n4$n5$n6$n7$n8" >> iphone.txt
done


