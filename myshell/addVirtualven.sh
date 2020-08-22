#!/bin/bash



if [ ! -d "$HOME/.virtualenvs" ];then
    echo "export WORKON_HOME=$HOME/.virtualenvs" >> $HOME/.zshrc
    echo "export PROJECT_HOME=$HOME/Devel">> $HOME/.zshrc
    echo "export VIRTUALENVWRAPPER_PYTHON=/usr/bin/python3" >> $HOME/.zshrc
    echo "source ~/.local/bin/virtualenvwrapper.sh" >> $HOME/.zshrc
fi
