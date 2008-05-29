#include <palacios/vmm_util.h>

#include <palacios/vmm.h>

extern struct vmm_os_hooks * os_hooks;


void PrintTraceHex(unsigned char x) {
  unsigned char z;
  
  z = (x >> 4) & 0xf;
  PrintTrace("%x", z);
  z = x & 0xf;
  PrintTrace("%x", z);
}

void PrintTraceLL(ullong_t num) {
  unsigned char * z = (unsigned char *)&num;
  int i;
  
  for (i = 7; i >= 0; i--) {
    PrintTraceHex(*(z + i));
  }
}


void PrintTraceMemDump(unsigned char *start, int n)
{
  int i, j;

  for (i = 0; i < n; i += 16) {
    PrintTrace("%8x", (unsigned)(start + i));
    for (j = i; (j < (i + 16)) && (j < n); j += 2) {
      PrintTrace(" ");
      PrintTraceHex(*((unsigned char *)(start + j)));
      if ((j + 1) < n) { 
	PrintTraceHex(*((unsigned char *)(start + j + 1)));
      }
    }
    PrintTrace(" ");
    for (j = i; (j < (i + 16)) && (j < n); j++) {
      PrintTrace("%c", ((start[j] >= 32) && (start[j] <= 126)) ? start[j] : '.');
    }
    PrintTrace("\n");
  }
}



