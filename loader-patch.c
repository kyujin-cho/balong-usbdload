#include <stdio.h>
#include <stdint.h>

#ifndef WIN32
//%%%%
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#else
//%%%%
#include <windows.h>
#include "getopt.h"
#include "printf.h"
#endif

#include "patcher.h"


//#######################################################################################################
void main(int argc, char* argv[]) {
  
FILE* in;
FILE* out;
uint8_t* buf;
uint32_t fsize;
int opt;
uint8_t outfilename[100];
int oflag=0,bflag=0;
uint32_t res;


// Разбор командной строки

while ((opt = getopt(argc, argv, "o:bh")) != -1) {
  switch (opt) {
   case 'h': 
     
printf("\n Программа для автоматического патча загрузчиков платформ Balong V7\n\n\
%s [ключи] <файл загрузчика usbloader>\n\n\
 Допустимы следующие ключи:\n\n\
-o file  - имя выходного файла. По умолчанию производится только проверка возможности патча\n\
-b       - добавить патч, отключающий проверку дефектных блоков\n\
\n",argv[0]);
    return;

   case 'o':
    strcpy(outfilename,optarg);
    oflag=1;
    break;

   case 'b':
     bflag=1;
     break;
     
   case '?':
   case ':':  
     return;
    
  }
}  

printf("\n Program of automatic modification of Balong V7 loaders, (c) forth32");

 if (optind>=argc) {
    printf("\n - No file name specified for downloading \n - Specify the -h key for prompting\n");
    return;
}  
    
in=fopen(argv[optind],"rb");
if (in == 0) {
  printf("\n Error opening file %s",argv[optind]);
  return;
}

// определяем размер файла
fseek(in,0,SEEK_END);
fsize=ftell(in);
rewind(in);

// выделяем буфер и читаем туда весь файл
buf=malloc(fsize);
fread(buf,1,fsize,in);
fclose(in);

//==================================================================================

res=pv7r1(buf, fsize);
if (res != 0)  {
  printf("\n* Found V7R1 type signature at offset %08x",res);
  goto endpatch;
}  

res=pv7r2(buf, fsize);
if (res != 0)  {
  printf("\n* Found V7R2 type signature at offset %08x",res);
  goto endpatch;
}  

res=pv7r11(buf, fsize);
if (res != 0)  {
  printf("\n* Found V7R11 type signature at offset %08x",res);
  goto endpatch;
}   

res=pv7r22(buf, fsize);
if (res != 0)  {
  printf("\n* Found V7R22 type signature at offset %08x",res);
  goto endpatch;
}  

res=pv7r22_2(buf, fsize);
if (res != 0)  {
  printf("\n* Found V7R22_2 type signature at offset %08x",res);
  goto endpatch;
}

res=pv7r22_3(buf, fsize);
if (res != 0)  {
  printf("\n* Found V7R22_3 type signature at offset %08x",res);
  goto endpatch;
}

printf("\n! No eraseall-patch signature found");

//==================================================================================
endpatch:

if (bflag) {
   res=perasebad(buf, fsize);
   if (res != 0) printf("\n* Found isbad signature at offset %08x",res);  
   else  printf("\n! The isbad signature was not found");  
}

if (oflag) {
  out=fopen(outfilename,"wb");
  if (out != 0) {
    fwrite(buf,1,fsize,out);
    fclose(out);
  }
  else printf("\n Error opening output file %s",outfilename);
}
free(buf);
printf("\n");
}

   