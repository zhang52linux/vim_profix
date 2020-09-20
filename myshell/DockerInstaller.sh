#!/bin/bash

# uninstall old version
sudo apt remove docker docker-engine docker.io containerd runc

#Install using the repository
sudo apt install -y \
    apt-transport-https \
    ca-certificates \
    curl \
    gnupg-agent \
    software-properties-common

#Add Docker’s official GPG key
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -

#Verify that you now have the key with the fingerprint
sudo apt-key fingerprint 0EBFCD88

# set up the stable repository
sudo add-apt-repository \
   "deb [arch=amd64] https://download.docker.com/linux/ubuntu \
   $(lsb_release -cs) \
   stable"

#INSTALL DOCKER ENGINE
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io

#update download repository
sudo mkdir -p /etc/docker
sudo tee /etc/docker/daemon.json <<-'EOF'
{
  "registry-mirrors": ["https://k9jzrhs6.mirror.aliyuncs.com"]
}
EOF
sudo systemctl daemon-reload
sudo systemctl restart docker


#Verify that Docker Engine is installed 
sudo docker pull hello-world
sudo docker run hello-world


