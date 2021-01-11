# kernel module of read/write generator
Tested: CentOS7, kernel version: 5.1.0

usage:

```
# ./install
# ./opendev /dev/xxx
# ./readdev @startPageNumber @nrPages
# ./writedev @startPageNumber @nrPages
# ./discarddev @startPageNumber @nrPages
# ./wsdev @startPageNumber @nrPages
# ./verifywrite @startPageNumber @nrPages @verifyNumber
# ./verifyread @startpageNumber @nrPages @verifyNumber
# ./writespecial @startPageNumber @nrPages @first_offset
# ./closedev
# ./remove
```

write variants(check out the shell script `writedev`)
```
w(s)(v)(p)(f) startPageNumber nrPages (verifyNum)
```

To turn on/off printk
```
# ./mute on
# ./mute off
```
