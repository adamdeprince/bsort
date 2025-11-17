Joule sort submission


This readme briefly describes my submission for the 2025 Daytona Joulesort competition.  

Bsort is a unique "inverted radix sort" for which the details of operation will be explained in a future paper.  It source can be found at [bsort.c](https://github.com/adamdeprince/bsort/blob/master/src/bsort.c).  It is compiled with a C compiler as: `gcc -O3 -march=native bsort.c -o bsort` and for our sort execution we ran bsort as `./bsort -a -c 10 -s 18 -k 10 -i input output` where -s and -c are tuning parameters described in `./bsort --help` that have a minor impact on performance.  Because this code is I/O bound compiler optimizations don't affect run time, but do affect CPU power consumption during the run. 

For the purposes of the Daytona search, the following flags adjust for differing record sizes.

* -a: Specifies the keys are 7-bit ascii.  The sort defaults to 8-bit clean.
* -r: The record size in bytes.  Defaults to 100
* -k: The size of the key portion of the record.  Defaults to 10

`-s` and `-c` are tuning parameters.  `-s` controls the depth of the relocation stack (to be explained in a future paper) and `-c` controls the threshold sort group size at which the algorithm switches from a radix sort to a shell sort. 

The sort code was run on a Raspberry Pi 5 with 16Gb ram equipted with the Raspberry Pi M.2 HAT equipped with a a Oyen Digital M/2 Series PCIe Gen x4 NVMe SSE of 4Tb in size.  The OS was Ubuntu 25.10.  The boot drive was the stock SD card shipped with the Raspbery and the entirely of the NVMe drive was dedicated to search and formatted as a btrfs filesystem.   It was unused prior to running the initial search, maximizing the number of unused blocks.  The board was enclosed in a "GeekPI" metal case and provided with a stock Raspberry PI cooling fan for the CPU.  The raspberry PI was connected to a generic USB power adapter capable of providing 5V and 5amps as required by the device. 

Power is measured by a [yocto-watt power meter](https://www.yoctopuce.com/EN/products/usb-electrical-sensors/yocto-watt) running the latest firmware, which has the same accuracy as a the BrandElectronics Model 20-1850/CI used in the original Joule Sort. The yocto-watt sensor is resetable and automatically measures power consumption without supervision, so no I/O was nessesary during the run. 

Execution was orchastracted by a shell script running on a MacBook Pro M4 Max.  The yocto-watt sensor was connected to the Macbook Pro by usb.   The following script was run five times, rebooting the device between executions to flush data from the backing store.  

```bash
#!/bin/bash
python [reset.py](https://github.com/adamdeprince/bsort/blob/master/reset.py)
ssh adam@192.168.1.59  "cd /fast && ulimit -s 1677721600 && time ./bsort -a -c 500 -s 12 -k 10 -i input  output"
python joules.py
```



Five passes were run, each taking approximately 6 hours.  

21,597 seconds, 47,548 joules
20,938 seconds, 48,048 joules
21,159 seconds, 48,319 joules
21,265 seconds, 48,335 joules
20,899 seconds, 47,599 joules

The median power consumption was 48,048 joules for a processing performance of 208,125 records per joule. 




While the sort was run on a raspberry 5, power meter reading and script launching was the responsibility of a host machine.   A aMacBook pro ran this script to start the yocto-watts power integrator, start the sort script on the raspberry 5, and when complete report on the number of joules consumed.   


When run the bsort took 21,597 seconds consuming 47,548 joules, for 210,313 recors per joule.
