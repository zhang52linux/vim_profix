#!/bin/bash


for ((i=1;i<10000;++i))
do
    {
        echo $i
    }&
done
wait
echo '执行ok'
clear
exit
