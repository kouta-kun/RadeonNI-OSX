/* The few C library functions hw/ needs; no OS under Open Firmware. */
#include <stddef.h>
#include <stdint.h>
void *memset(void *d, int c, size_t n){unsigned char*p=d;while(n--)*p++=(unsigned char)c;return d;}
void *memcpy(void *d, const void *s, size_t n){unsigned char*p=d;const unsigned char*q=s;while(n--)*p++=*q++;return d;}
void *memmove(void *d, const void *s, size_t n){unsigned char*p=d;const unsigned char*q=s;
	if(p<q){while(n--)*p++=*q++;}else{p+=n;q+=n;while(n--)*--p=*--q;}return d;}
int memcmp(const void *a, const void *b, size_t n){const unsigned char*p=a,*q=b;
	for(;n;n--,p++,q++)if(*p!=*q)return *p-*q;return 0;}
size_t strlen(const char *s){size_t n=0;while(s[n])n++;return n;}
int strcmp(const char *a, const char *b){while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
int strncmp(const char *a, const char *b, size_t n){for(;n;n--,a++,b++){if(*a!=*b)return (unsigned char)*a-(unsigned char)*b;if(!*a)return 0;}return 0;}
char *strncpy(char *d, const char *s, size_t n){size_t i=0;for(;i<n&&s[i];i++)d[i]=s[i];for(;i<n;i++)d[i]=0;return d;}
