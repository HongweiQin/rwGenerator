# kernel module of read/write generator
Tested: CentOS7, kernel version: 4.16.0

usage:
```
# ./install
# ./openpblk
# ./readdev @startPageNumber @nrPages
# ./writedev @startPageNumber @nrPages
# ./wsdev @startPageNumber @nrPages
# ./verifywrite @startPageNumber @nrPages @verifyNumber
# ./verifyread @startpageNumber @nrPages @verifyNumber
# ./writespecial @startPageNumber @nrPages @first_offset
# ./remove
```
To turn on/off printk
```
# ./mute on
# ./mute off
```
