#!/bin/bash

if [ "$EUID" -ne 0 ]
  then echo "Please run as root!"
  exit
fi

echo 'Prepare system'

sys=`cat /etc/os-release | grep NAME`
ubuntu=`echo $sys | grep Ubuntu`
centos=`echo $sys | grep CentOS`
debian=`echo $sys | grep Debian`
fedora=`echo $sys | grep Fedora`

if [ "$centos" ] || [ "$fedora" ]; then
        yum -y install perl-devel perl-ExtUtils-CBuilder perl-ExtUtils-MakeMaker patchutils patch
        xx=`which gcc`
        xx=`echo $xx | grep gcc`
        if [ ! $xx ]; then
                        yum install gcc
        fi
        xx=`which make`
  xx=`echo $xx | grep make`
  if [ ! $xx ]; then
                        yum install make
  fi
  xx=`which unzip`
        xx=`echo $xx | grep unzip`
        if [ ! $xx ]; then
                        yum install unzip
        fi

elif [ "$ubuntu" ] || [ "$debian" ]; then
        apt-get install libproc-processtable-perl
        apt-get install libelf-dev
        xx=`which patch`
  xx=`echo $xx | grep patch`
  if [ ! $xx ]; then
                        apt-get install patchutils patch
  fi

        xx=`which gcc`
  xx=`echo $xx | grep gcc`
  if [ ! $xx ]; then
                        apt-get install gcc
  fi

  xx=`which make`
  xx=`echo $xx | grep make`
  if [ ! $xx ]; then
                        apt-get install make
  fi

fi

echo "V4L drivers building..."
make -j$(nproc)
echo "V4L drivers installing..."

echo "Installing modules..."

make install
echo "V4L drivers installation done"
echo "install the firmware"
if [ -f tbs-tuner-firmwares*.tar.bz2 ]; then
		tar jxvf tbs-tuner-firmwares*.tar.bz2 -C /lib/firmware/
else
		echo "firmware file not exist!!!"
fi

echo "done. please reboot the system."
