Joule sort submission


This readme briefly describes my submission for the 2024 Indy Joulesort competition.  While implementation of a sorting algorithm, [sort](https://github.com/adamdeprince/bsort) is capable of entering the Daytona competition, it was not tested that way in preparation for this competition. 

Bsort is a unique "inverted radix sort" for which the details of operation will be explained in a future paper.  It source can be found at [bsort.c](https://github.com/adamdeprince/bsort/blob/master/src/bsort.c).  It is compiled with a C compiler as: `gcc -O3 -march=native bsort.c -o bsort` and for our sort execution we ran bsort as `./bsort -a -c 10 -s 18 -k 10 -i input output` where -s and -c are tuning parameters descried in `./bsort --help` that have a minor impact on performance. 

For the purpose of the completion the sort code was run on a raspberry pi 5 with 8Gb ram equipted with the Raspberry Pi M.2 HAT equipped with a a Oyen Digital M/2 Series PCIe Gen x4 NVMe SSE of 4Tb in size.  The OS was Ubuntu 24.10.  The boot drive the SD card shipped with the Raspbery and the entirely of the NVMe drive was dedicated to search.   It was unused prior to running the initial search, maximizing the number of unused blocks. 

Power is measured by a [yocto-watt power meter](https://www.yoctopuce.com/EN/products/usb-electrical-sensors/yocto-watt) running the latest firmware, which according to the manufacturer has 1% accuracy.  

Bsort mmemaps the input and output file.  The input file is read once and copied into the output file where the sort is run.  Bsort is a sort in place algorithm.  Due to the limited memory capacity of the Raspberry compared to the 1Tb output file the oom killer is frequently invoked despite the fact no memory is being allocated.   To prevent this, shortly after starting the bsort program the command `echo -17 > /proc/<pid>/oom_adj` was run to prevent the sort from being ejected by the oom killer.   This along with `echo 2 > /proc/sys/vm/overcommit_memory` allowed the sort to run to completion. 

While the sort was run on a raspberry 5, power meter reading and script launching was the responsibility of a host machine.   A aMacBook pro ran this script to start the yocto-watts power integrator, start the sort script on the raspberry 5, and when complete report on the number of joules consumed.   

```bash
source ~/bsort-ve/bin/activate
python reset.py
ssh adamdeprince@192.168.1.37  "bash -c 'cd /fast;./bsort -a -c 10 -s 18 -k 10 -i input output'"
python joules.py
```

When run the bsort took 21,597 seconds consuming 47,548 joules, for 210,313 recors per joule.
