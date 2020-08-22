#!/bin/bash

i=1
while [ $i -lt 30 ]
do
    echo "$i"
    let i+=1
    sleep 3
done
