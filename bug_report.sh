#!/bin/bash

gather_info()
{
cat<<EOF
This script will gather some data to find problems with the drivers
EOF
cat<<EOF
==================================================================
EOF

cat<<EOF
Operating system
EOF
lsb_release -a
cat<<EOF
------------------------------------------------------------------
EOF

cat<<EOF
Neumo version
EOF
cat /sys/module/dvb_core/info/version
cat<<EOF
------------------------------------------------------------------
EOF

cat<<EOF
Installed neumo modules
EOF
ls -alR /lib/modules/`uname -r`/updates/neumo
cat <<EOF
------------------------------------------------------------------
EOF


cat<<EOF
Loaded modules
EOF
lsmod
cat <<EOF
------------------------------------------------------------------
EOF

cat<<EOF
Present adapters
EOF
ls -alR /dev/dvb/
cat <<EOF
------------------------------------------------------------------
EOF

cat<<EOF
Present usb devices
EOF
sudo lsusb -v
cat <<EOF
------------------------------------------------------------------
EOF

cat<<EOF
Present pci devices
EOF
sudo lspci -v
cat <<EOF
------------------------------------------------------------------
EOF


cat<<EOF
Module parameters
EOF
for module in stid135 tbsecp3 m88rs6060 dvb_core; do echo $module;echo  "+++++"; (cd  /sys/module/$module/parameters/; for p in  `ls `; do echo $p="$(cat $p)"; done;); echo ;done
cat<<EOF
------------------------------------------------------------------
EOF

cat<<EOF
Any recent kernel craches
EOF
sudo grep RIP /var/log/debug |tail -10
cat <<EOF
------------------------------------------------------------------
EOF

cat<<EOF
Any recent kernel bugs
EOF
sudo grep BUG /var/log/debug |tail -10
cat <<EOF
------------------------------------------------------------------
EOF
}

output=`mktemp -d`
gather_info > $output/bug_report
sudo tail -c20000000 /var/log/debug* > $output/
sudo tail -c20000000 /var/log/syslog* > $output/
sudo tail -c20000000 /var/log/kern* > $output/
cp /tmp/neumo.log $output;
set -x
tar -zcf /tmp/bugreport.tar.gz --transform "s%${output#/}%xxx%" $output
rm -fr $output

echo<<EOF
Essential log data was written to /tmp/bugreport.tar.gz
Do not forget to add a clear description of the bug itself.
Which commands did you run?
What did you try to achieve?
What went wrong?

EOF
