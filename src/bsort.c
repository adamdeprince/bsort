 #include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>


int verbosity;


struct sort {
  int fd;
  off_t size;
  void *buffer;
};


static unsigned long getTick(void) {
  struct timespec ts;
  unsigned theTick = 0U;
  clock_gettime( CLOCK_REALTIME, &ts );
  theTick  = ts.tv_nsec / 1000000;
  theTick += ts.tv_sec * 1000;
  return theTick;
}


static inline void
shellsort(unsigned char *a,
          const int n,
          const int record_size,
          const int key_size) {
  char temp[record_size];

  int j;
  for (int gap = n/2; gap > 0; gap /= 2) {
    for (int i = gap; i < n; i++) {
      memcpy(temp, a+i*record_size, record_size);
      for (j = i; j >= gap && memcmp(a + (j - gap) * record_size, temp, record_size) >= 0; j -= gap) {
	memcpy(a + j*record_size, a + (j-gap) * record_size, record_size);
      }
      memcpy(a + j*record_size, temp, record_size);
    }
  }
}


int compare(int *length, unsigned char *a, unsigned char *b) {
  return memcmp(a, b, *length);
}

void
radixify(register unsigned char *buffer,
         const long count,
	 unsigned char *inbuffer,
         const long digit,
         const int char_start,
         const int char_stop,
         const long record_size,
         const long key_size,
         const long stack_size,
	 const long cut_off,
	 long *previously_prepared_counts) {
  long counts[char_stop+1];
  long prepared_counts[char_stop+1][char_stop+1];
  long offsets[char_stop+1];
  long starts[char_stop+1];
  long ends[char_stop+1];
  long offset=0;
  unsigned char temp[record_size];
  long target, x, a, b;
  long stack[stack_size+1];
  long stack_pointer = 0;
  long last_position, last_value, next_value;
  printf("Offset: %ld, digit:%ld, count:%ld, start:%d, stop:%d\n", offset, digit, count, char_start, char_stop);
  for (x=char_start; x<=char_stop; x++) {
    counts[x] = 0;
    offsets[x] = 0;
  }

  long total = 0;
  // Compute starting positions
  if (digit == 0) {
    for (x=0; x<count; x++) {
      long c = inbuffer[x*record_size];
      counts[c] += 1;
      memcpy(buffer+x*record_size, inbuffer+x*record_size, record_size);
    }

    munmap(inbuffer, x * record_size);
   
  } else {
    if (!previously_prepared_counts) {
      for (x=0; x<count; x++) {
	long c = buffer[x*record_size + digit];
	counts[c] += 1;
	total += c;
      }
    } else {
      memcpy(&counts, previously_prepared_counts, sizeof(long) * (char_stop+1));
      for(x=0;x<char_stop;x++) {
	total += counts[x];
      }
    }
    printf("count: %ld, TOTAL: %ld\n", count, total);
	    
  }
  for (x=char_start;x < char_stop; x++) {
    for (int y=char_start; y< char_stop; y++) {
      prepared_counts[x][y] = 0;
    }
  }
  
  
  // Compute offsets
  offset = 0;
  for(x=char_start; x<char_stop; x++) {
    offsets[x] = offset;
    starts[x] = offsets[x];
    offset += counts[x];
  }
  
  for(x=char_start; x<char_stop; x++) {
    ends[x] = offsets[x+1];
  }
  ends[char_stop] = count;
  
  for(x=char_start; x<char_stop; x++) {
    printf("====%ld\n", x);
    while (offsets[x] < ends[x]) {
      prepared_counts[x][buffer[offsets[x] * record_size + digit + 1]] += 1;
      printf("%ld %ld: %hhu\n", x, offsets[x], buffer[offsets[x] * record_size + digit + 1]);
      if (buffer[offsets[x] * record_size + digit] == x) {
	offsets[x] += 1;
      } else {
	stack_pointer=0;
	stack[stack_pointer] = offsets[x];
	stack_pointer += 1;
	target = buffer[offsets[x] * record_size + digit];
	
	while( target != x && stack_pointer < stack_size ) {
	  stack[stack_pointer] = offsets[target];
	  offsets[target] += 1;
	  
	  target = buffer[stack[stack_pointer] * record_size + digit];
	  stack_pointer++;
	};
	if (target == x) {
	  offsets[x] += 1;
	}
	stack_pointer--;
	memcpy(&temp, &buffer[stack[stack_pointer] * record_size], record_size);
	while (stack_pointer) {
	  memcpy(&buffer[stack[stack_pointer] * record_size], &buffer[stack[stack_pointer-1] * record_size], record_size);
	  stack_pointer--;
	}
        memcpy(&buffer[stack[0] * record_size], &temp, record_size);
      }
    }
  }
  
  for(x=char_start; x<char_stop; x++) {
    if ( ends[x] - starts[x] > cut_off) {
      printf("%ld:  ", x);
      for(int y=char_start; y<char_stop; y++) {
	printf("%ld ", prepared_counts[x][y]);
      }
      printf("\n");
      radixify(&buffer[starts[x] * record_size],
	       ends[x] - starts[x],
	       0,
	       digit+1,
	       char_start,
	       char_stop,
	       record_size,
	       key_size,
	       stack_size,
	       cut_off,
	       prepared_counts[x]);
    } else {
      if (ends[x] - starts[x] <= 1) continue;
      shellsort(&buffer[starts[x] * record_size], ends[x] - starts[x], record_size, key_size);
      //qsort_r(&buffer[starts[x] * record_size], ends[x] - starts[x], record_size, &compare, &record_size);
    }
  }
}

  


int read_sort(char *path, struct sort *sort) {
  void *buffer = NULL;

  int fd = open(path, O_RDONLY);
  if (fd == -1)
    goto error;

  struct stat stats;
  if (-1 == fstat(fd, &stats))
    goto error;


  if (!(buffer = mmap(NULL,
                      stats.st_size,
                      PROT_READ,
                      MAP_SHARED,
                      fd,
                      0
                      )))
    goto error;
  madvise(buffer, stats.st_size, POSIX_MADV_WILLNEED | POSIX_MADV_SEQUENTIAL);

  sort->buffer = buffer;
  sort->size = stats.st_size;
  sort->fd = fd;
  return 0;

  
  
error:
  perror(path);
  if (buffer)
    munmap(buffer, stats.st_size);

  if (fd != -1)
    close(fd);
  sort->buffer = 0;
  sort->fd = 0;
  return -1;
}


int open_sort(char *path, struct sort *sort) {
  void *buffer = NULL;

  int fd = open(path, O_RDWR);
  if (fd == -1)
    goto error;

  struct stat stats;
  if (-1 == fstat(fd, &stats))
    goto error;
  
  
  if (!(buffer = mmap(NULL,
                      stats.st_size,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      fd,
                      0
                      )))
    goto error;
  madvise(buffer, stats.st_size, POSIX_MADV_WILLNEED | POSIX_MADV_SEQUENTIAL);

  sort->buffer = buffer;
  sort->size = stats.st_size;
  sort->fd = fd;
  return 0;
  
error:
  perror(path);
  if (buffer)
    munmap(buffer, stats.st_size);
  if (fd != -1)
    close(fd);
  sort->buffer = 0;
  sort->fd = 0;
  return -1;
}



int create_sort(char *path, struct sort *sort, struct sort *input) {
  void *buffer = NULL;

  int fd = open(path, O_RDWR | O_CREAT, 0644);
  if (fd == -1)
    goto error;
  if (-1 == lseek(fd, input->size-1, SEEK_SET))
    goto error;

  if (-1 == write(fd, " ", 1))
    goto error;

  struct stat stats;
  if (-1 == fstat(fd, &stats))
    goto error;
  
  if (!(buffer = mmap(NULL,
                      stats.st_size,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      fd,
                      0
		      )))
    goto error;

  madvise(buffer, stats.st_size, POSIX_MADV_WILLNEED | POSIX_MADV_SEQUENTIAL);

  sort->buffer = buffer;
  sort->size = stats.st_size;
  sort->fd = fd;
  return 0;
  
error:
  perror(path);
  if (buffer)
    munmap(buffer, stats.st_size);
  if (fd != -1)
    close(fd);
  sort->buffer = 0;
  sort->fd = 0;
  return -1;
}


void close_sort(struct sort *sort) {
  if (sort->buffer) {
    munmap(sort->buffer, sort->size);
    sort->buffer = 0;
    sort->size = 0;
  }

  if (sort->fd) {
    close(sort->fd);
    sort->fd = 0;
  }
}


int
main(int argc, char *argv[]) {
  struct rlimit rl;
  rl.rlim_cur = 256 * 256 * 8 * 5 * 10;
  rl.rlim_max = 256 * 256 * 8 * 5 * 10;
  if (-1 == setrlimit(RLIMIT_STACK, &rl)) {
    fprintf(stderr, "Failed to grow stack\n");
    goto failure;
  }

  int opt;
  int char_start = 0;
  int char_stop = 255;
  int record_size=100;
  int key_size=10;
  int stack_size=12;
  int cut_off = 1000;
  char *infile=0;
  char *outfile=0;
  verbosity = 0;
  int single = 0;
    
  while ((opt = getopt(argc, argv, "i:var:k:s:c:o:S")) != -1) {
    switch (opt) {
    case 'v':
      verbosity += 1;
      break;
    case 'a':
      char_start = 32;
      char_stop = 128;
      break;
    case 'r':
      record_size = atoi(optarg);
      break;
    case 'k':
      key_size = atoi(optarg);
      break;
    case 's':
      stack_size = atoi(optarg);
      break;
    case 'c':
      cut_off = atoi(optarg);
      break;
    case 'i':
      infile = strdup(optarg);
      fprintf(stderr, "input file:%s\n", optarg);
      break;
    case 'o':
      outfile = strdup(optarg);
      fprintf(stderr, "output file:%s\n", optarg);
      break;
    case 'S':
      single = 1;
      break;
    default:
      fprintf(stderr, "Invalid parameter: -%c\n", opt);
      goto failure;
    }
  }


  unsigned long TickStart = getTick();

  if (verbosity)
    printf("sorting %s\n", argv[optind]);
  
  struct sort insort;
  struct sort outsort;
  if (infile) {
    if (-1==read_sort(infile, &insort)) {
      fprintf(stderr, "Failed to open input");
      goto failure;
    }
    
    if (-1==create_sort(outfile, &outsort, &insort)) {
      fprintf(stderr, "Failed to open output file");
      goto failure;
    }
  }
  fprintf(stderr,"Key size: %d\n", key_size);
  radixify(outsort.buffer,
	   outsort.size / record_size,
	   insort.buffer,
	   0,
	   char_start,
	   char_stop,
	   record_size,
	   key_size,
	   stack_size,
	   cut_off,
	   NULL);

  optind++;
  
  
  printf("Processing time: %.3f s\n", (float)(getTick() - TickStart) / 1000);

  exit(0);
failure:
  fprintf(stderr,
          "Usage: %s [-v] [-a] [-r ###] [-k ###] [-s ###] -i [infile] -o [file] \n",
          argv[0]);
  fprintf(stderr,
          "Individually sort binary files inplace with a radix sort\n"
          "\n"
          "Sorting Options:\n"
          "\n"
          "  -a       assume files are printable 7-bit ascii instead of binary\n"
          "  -k ###   size of compariable section of record, in bytes (default 100)\n"
          "  -r ###   size of overall record, in bytes.  (default 100)\n"
          "\n"
          "Options:\n"
          "  -i fname input filename.  Required for competition\n"
          "  -v  verbose output logging\n"
          "\n"
          "Tuning Options:\n"
          "\n"
          "  -s ###   pushahead stack size.  (default 12)\n"
          "  -c ###   buffer size after which to use shell sort (defaults to 3000)\n"
          "\n"
          "Report bsort bugs to adam.deprince@gmail.com\n"
          );
  exit(1);
}
    



