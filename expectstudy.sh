!/bin/bash

git add *
git commit -m "add linux.pptx"


while read username password 
do
/usr/bin/expect <<-EOF 
spawn git push origin master
expect {
    "Username" { send "$username\r"; exp_continue }
    "Password" { send "$password\r" }
}
expect "#"
expect eof
EOF
done < ~/passwd.txt






