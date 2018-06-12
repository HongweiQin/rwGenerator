# kernel module of read/write generator
Tested: CentOS7, kernel version: 4.14.0

usage:
```
# ./install
# ./openpblk
# ./readdev @startPageNumber @nrPages
# ./writedev @startPageNumber @nrPages
# ./wsdev @startPageNumber @nrPages
# ./verifywrite @startPageNumber @nrPages @verifyNumber
# ./verifyread @startpageNumber @nrPages @verifyNumber
# ./remove
```
