Joule sort Daytona submission

This readme briefly describes my submission for the 2025 Daytona Joulesort competition.  

Bsort is a unique "inverted radix sort".  It source can be found at [bsort.c](https://github.com/adamdeprince/bsort/blob/master/src/bsort.c).  It is compiled with a C compiler as: `gcc -O3 -march=native bsort.c -o bsort` and for our sort execution we ran bsort as `./bsort -a -c 10 -s 18 -k 10 -i input output` where -s and -c are tuning parameters described in `./bsort --help` that have a minor impact on performance.  Because this code is I/O bound compiler optimizations don't affect run time, but do affect CPU power consumption during the run. 

For the purposes of the Daytona search, the following flags adjust for differing record sizes.

* `-a`: Specifies the keys are 7-bit ascii.  The sort defaults to 8-bit clean.
* `-r`: The record size in bytes.  Defaults to 100
* `-k`: The size of the key portion of the record.  Defaults to 10

# Tuning parameters: 

* `-s` controls the depth of the relocation stack (to be explained in a future paper)
* `-c` controls the threshold sort group size at which the algorithm switches from a radix sort to a shell sort.

# Algorithm: 

The sort algorithm is a straightforward radix sort, except it starts with the most significant byte.  In the initial pass a copy is made from the input to output to comply with the rules of the Joulesort that require separate input and output files.   During this initial pass a histogram of the first bytes of the keys are made.

After the copy is the sort phase.  The sort phase is called recursively, each level receiving a start index, end index and histogram from the previous level.   For the initial sort phase the histogram from the copy and indices representing the entirely of the file are passed to the sort function.

From the histogram the sort function computes the offsets at which each letter will start.  Assuming we're working in ASCII, the space character goes first and is marked as starting at offset 0 and ending at the number of spaces indicated in the histogram.   The next character `!`, starts at the number of spaces and ends at the number of spaces plus the number of exclamation points.  The next character `"` starts at the offset equal to the number of spaces plus the number of exclamation points, and ends at the number of spaces plus the number of exclamation points, plus the number of double quotes.  This process is continued to build an offset start and offset ending table for all of the characters. 

The sort begins by reading the first row - assign to this first row a "swap pointer" that indicates the current row we are working on.  If the first character a space, we increment our space offset start by one and increment our swap pointer by one.  If its not a space  we note its first letter and swap the record with the record at the offset start for that first letter.  We then increment the offset start for that first letter so our swapped row is not over written and continue the process.  

Eventually as myriad indices are advanced for rows swapped through the first row, you'll encounter a records that starts with a space.  When that happens, you increment the "swap pointer" from the first row to the second row and so forth.  The process is continued until the row index reaches the spaces ending offset, at which you move to `!` and repeat the process. 

Gradually the algorithm will work its way through all of the records, speeding up as it goes because with each successful letter the number of rows already in the right place are in order. 

A histogram of the second letter should be made.  This histogram should be on a per letter basic, so for example you could have n histograms for n letters in your keys character set.  The first a histogram of second letters for the first letter ` `, the second a histogram of second letters for the first letter `!` and so forrth.  This histogram can be made during the sort pass or during a separate scan of the data at the start of the sort call.  Whichever is used is up to the implementer, but doing so inline is slightly faster as it requires fewer passes over the data. 

Once the sort is complete your rows should be in their correct location in so far as the first letter is concerned, and you move to the second.   The sort algorithm is called once for each letter - its range of operation should start at the offset-start of each letter and end at the offset-end of each letter instead of the entire file as in the first pass.  The sort should receive the histogram for its respective letter, or compute its own as you please.  So for example the sort of "first letters that are spaces" will start at 0, go to the number of spaces you had in the first pass, and include a histogram of second letters for records starting with a space.  This algorithm should be repeated recursively for each letter.  On each pass the number of items the sort algorithm is to sort per pass will drop by approximately a factor of the cardinality of the character set.   For the second letter you should expect the sort algorithm to be called n times, for the third letter, n^2 times on increasingly small sections of data where the rows are in order with respect to their starting letter(s).   

When you've reached the last letter of the key, or all of your datasets to sort have one or less records, you're done. 

# Optimizations:

There are a few optimizations you cam make.   For recursive calls the sort function will eventually be asked to sort a very small number of rows.  This will work, but on modern hardware its more efficient to switch to a shell sort when the size of the row collection to sort drops below about 1000. 

Row swapping can be optimized somewhat to reduce the number of memory copies in half.   Instead of swapping two rows each pass, a note of what row will be swapped can be pushed onto a stack.   When the stack his a predefined limit, controllable by the `-s` flag in bsort, it is unrolled and records are 'rolled' from one to the next.  So a->b, b->c, c->d, d->e, e->a instead of performing individual swaps.   This saves CPU which improves the power consumption somewhat, but it's necessary to implement the core algorithm. 

Computing the histogram inline will reduce the number of reads from 2n where n is the number of characters in the key to n+1.  This makes a small difference in run time as on modern hardware the wall time is dominated by SSD writes.

# Hardware:

The sort code was run on a Raspberry Pi 5 with 16Gb ram equipted with the Raspberry Pi M.2 HAT equipped with a a Oyen Digital M/2 Series PCIe Gen x4 NVMe SSE of 4Tb in size.  The OS was Ubuntu 25.10.  The boot drive was the stock SD card shipped with the Raspbery and the entirely of the NVMe drive was dedicated to search and formatted as a btrfs filesystem.   It was unused prior to running the initial search, maximizing the number of unused blocks.  The board was enclosed in a "GeekPI" metal case and provided with a stock Raspberry PI cooling fan for the CPU.  The raspberry PI was connected to a generic USB power adapter capable of providing 5V and 5amps as required by the device. 

Power is measured by a [yocto-watt power meter](https://www.yoctopuce.com/EN/products/usb-electrical-sensors/yocto-watt) running the latest firmware, which has the same accuracy as a the BrandElectronics Model 20-1850/CI used in the original Joule Sort. The yocto-watt sensor is resettable and automatically measures power consumption without supervision, so no I/O was necessary during the run. 

# Execution: 

Execution was orchestrated by a shell script running on a MacBook Pro M4 Max.  The yocto-watt sensor was connected to the Macbook Pro by usb.   The following script was run five times, rebooting the device between executions to flush data from the backing store.   

```bash
#!/bin/bash
python reset.py # https://github.com/adamdeprince/bsort/blob/master/reset.py
ssh adam@192.168.1.59  "cd /fast && ulimit -s 1677721600 && time ./bsort -a -c 500 -s 12 -k 10 -i input  output"
python joules.py # https://github.com/adamdeprince/bsort/blob/master/joules.py
```

# Performance: 

Five passes were run, each taking approximately 6 hours.  

21,597 seconds, 47,548 joules
20,938 seconds, 48,048 joules
21,159 seconds, 48,319 joules
21,265 seconds, 48,335 joules
20,899 seconds, 47,599 joules

The median power consumption was 48,048 joules for a processing performance of 208,125 records per joule. 

bsort took 21,597 seconds consuming 47,548 joules, for 210,313 records per joule.

The results of valsort are: 

```
./valsort input
First unordered record is record 2
Records: 10000000000
Checksum: 12a06cd06eeb64b16
ERROR - there are 4999998439 unordered records

$ ./valsort output
Records: 10000000000
Checksum: 12a06cd06eeb64b16
Duplicate keys: 0
SUCCESS - all records are in order
```
