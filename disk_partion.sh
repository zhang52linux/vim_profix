#!/bin/bash

#Author: szw_zhang
#Created Time: 2020-06-06
#Script Description: harddisk parttion script



fdisk /dev/sdb << EOF
n
p
2

+4096M
w
EOF

mkfs -t xfs /dev/sdb2


lsblk -f
