// Загрузчик usbloader.bin через аварийный порт для модемов на платформе Balong V7R2.
//
//
#include <stdio.h>
#include <stdint.h>

#ifndef WIN32
//%%%%
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <arpa/inet.h>
#else
//%%%%
#include <windows.h>
#include <setupapi.h>
#include "getopt.h"
#include "printf.h"
#endif

#include "parts.h"
#include "patcher.h"


#ifndef WIN32
int siofd;
struct termios sioparm;
#else
static HANDLE hSerial;
#endif
FILE* ldr;


//*************************************************
//* HEX-дамп области памяти                       *
//*************************************************

void dump(unsigned char buffer[],int len) {
int i,j;
unsigned char ch;

printf("\n");
for (i=0;i<len;i+=16) {
  printf("%04x: ",i);
  for (j=0;j<16;j++){
   if ((i+j) < len) printf("%02x ",buffer[i+j]&0xff);
   else printf("   ");}
  printf(" *");
  for (j=0;j<16;j++) {
   if ((i+j) < len) {
    // преобразование байта для символьного отображения
    ch=buffer[i+j];
    if ((ch < 0x20)||((ch > 0x7e)&&(ch<0xc0))) putchar('.');
    else putchar(ch);
   } 
   // заполнение пробелами для неполных строк 
   else printf(" ");
  }
  printf("*\n");
 }
}


//*************************************************
//* Рассчет контрольной суммы командного пакета
//*************************************************
void csum(unsigned char* buf, int len) {

unsigned  int i,c,csum=0;

unsigned int cconst[]={0,0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7, 0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF};

for (i=0;i<(len-2);i++) {
  c=(buf[i]&0xff);
  csum=((csum<<4)&0xffff)^cconst[(c>>4)^(csum>>12)];
  csum=((csum<<4)&0xffff)^cconst[(c&0xf)^(csum>>12)];
}  
buf[len-2]=(csum>>8)&0xff;
buf[len-1]=csum&0xff;
  
}

//*************************************************
//*   Отсылка командного пакета модему
//*************************************************
int sendcmd(unsigned char* cmdbuf, int len) {

unsigned char replybuf[1024];
unsigned int replylen;

#ifndef WIN32
csum(cmdbuf,len);
write(siofd,cmdbuf,len);  // отсылка команды
tcdrain(siofd);
replylen=read(siofd,replybuf,1024);
#else
    DWORD bytes_written = 0;
    DWORD t;

    csum(cmdbuf, len);
    WriteFile(hSerial, cmdbuf, len, &bytes_written, NULL);
    FlushFileBuffers(hSerial);

    t = GetTickCount();
    do {
        ReadFile(hSerial, replybuf, 1024, (LPDWORD)&replylen, NULL);
    } while (replylen == 0 && GetTickCount() - t < 1000);
#endif
if (replylen == 0) return 0;    
if (replybuf[0] == 0xaa) return 1;
return 0;
}

//*************************************
// Открытие и настройка последовательного порта
//*************************************

int open_port(char* devname) {

//============= Linux ========================  
#ifndef WIN32

int i,dflag=1;
char devstr[200]={0};

// Вместо полного имени устройства разрешается передавать только номер ttyUSB-порта

// Проверяем имя устройства на наличие нецифровых символов
for(i=0;i<strlen(devname);i++) {
  if ((devname[i]<'0') || (devname[i]>'9')) dflag=0;
}
// Если в строке - только цифры, добавляем префикс /dev/ttyUSB
if (dflag) strcpy(devstr,"/dev/ttyUSB");
// копируем имя устройства
strcat(devstr,devname);

siofd = open(devstr, O_RDWR | O_NOCTTY |O_SYNC);
if (siofd == -1) return 0;

bzero(&sioparm, sizeof(sioparm)); // готовим блок атрибутов termios
sioparm.c_cflag = B115200 | CS8 | CLOCAL | CREAD ;
sioparm.c_iflag = 0;  // INPCK;
sioparm.c_oflag = 0;
sioparm.c_lflag = 0;
sioparm.c_cc[VTIME]=30; // timeout  
sioparm.c_cc[VMIN]=0;  
tcsetattr(siofd, TCSANOW, &sioparm);
return 1;

//============= Win32 ========================  
#else
    char device[20] = "\\\\.\\COM";
    DCB dcbSerialParams = {0};
    COMMTIMEOUTS CommTimeouts;

    strcat(device, devname);
    
    hSerial = CreateFileA(device, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (hSerial == INVALID_HANDLE_VALUE)
        return 0;

    ZeroMemory(&dcbSerialParams, sizeof(dcbSerialParams));
    dcbSerialParams.DCBlength=sizeof(dcbSerialParams);
    dcbSerialParams.BaudRate=CBR_115200;
    dcbSerialParams.ByteSize=8;
    dcbSerialParams.StopBits=ONESTOPBIT;
    dcbSerialParams.Parity=NOPARITY;
    dcbSerialParams.fBinary = TRUE;
    dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
    dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;
    if(!SetCommState(hSerial, &dcbSerialParams))
    {
        CloseHandle(hSerial);
        return 0;
    }

    CommTimeouts.ReadIntervalTimeout = MAXDWORD;
    CommTimeouts.ReadTotalTimeoutConstant = 0;
    CommTimeouts.ReadTotalTimeoutMultiplier = 0;
    CommTimeouts.WriteTotalTimeoutConstant = 0;
    CommTimeouts.WriteTotalTimeoutMultiplier = 0;
    if (!SetCommTimeouts(hSerial, &CommTimeouts))
    {
        CloseHandle(hSerial);
        return 0;
    }

    return 1;
#endif
}

//*************************************
//* Поиск linux-ядра в образе раздела
//*************************************
int locate_kernel(char* pbuf, uint32_t size) {
  
int off;

for(off=(size-8);off>0;off--) {
  if (strncmp(pbuf+off,"ANDROID!",8) == 0) return off;
}
return 0;
}

#ifdef WIN32

DEFINE_GUID(GUID_DEVCLASS_PORTS, 0x4D36E978, 0xE325, 0x11CE, 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18);

static int find_port(int* port_no, char* port_name)
{
  HDEVINFO device_info_set;
  DWORD member_index = 0;
  SP_DEVINFO_DATA device_info_data;
  DWORD reg_data_type;
  char property_buffer[256];
  DWORD required_size;
  char* p;
  int result = 1;

  device_info_set = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, 0, DIGCF_PRESENT);

  if (device_info_set == INVALID_HANDLE_VALUE)
    return result;

  while (TRUE)
  {
    ZeroMemory(&device_info_data, sizeof(SP_DEVINFO_DATA));
    device_info_data.cbSize = sizeof(SP_DEVINFO_DATA);

    if (!SetupDiEnumDeviceInfo(device_info_set, member_index, &device_info_data))
      break;

    member_index++;

    if (!SetupDiGetDeviceRegistryPropertyA(device_info_set, &device_info_data, SPDRP_HARDWAREID,
             &reg_data_type, (PBYTE)property_buffer, sizeof(property_buffer), &required_size))
      continue;

    if (strstr(_strupr(property_buffer), "VID_12D1&PID_1443") != NULL)
    {
      if (SetupDiGetDeviceRegistryPropertyA(device_info_set, &device_info_data, SPDRP_FRIENDLYNAME,
              &reg_data_type, (PBYTE)property_buffer, sizeof(property_buffer), &required_size))
      {
        p = strstr(property_buffer, " (COM");
        if (p != NULL)
        {
          *port_no = atoi(p + 5);
          strcpy(port_name, property_buffer);
          result = 0;
        }
      }
      break;
    }
  }

  SetupDiDestroyDeviceInfoList(device_info_set);

  return result;
}

#endif

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

void main(int argc, char* argv[]) {

unsigned int i,res,opt,datasize,pktcount,adr;
int bl;    // текущий блок
unsigned char c;
int fbflag=0, tflag=0, mflag=0, bflag=0, cflag=0;
int koff;  // смещение до ANDROID-заголовка
char ptfile[100];

FILE* pt;
char ptbuf[2048];
uint32_t ptoff;

struct ptable_t* ptable;

unsigned char cmdhead[14]={0xfe,0, 0xff};
unsigned char cmddata[1040]={0xda,0,0};
unsigned char cmdeod[5]={0xed,0,0,0,0};

// список разделов, которым нужно установить файловый флаг
uint8_t fileflag[41];

struct {
  int lmode;  // режим загрузки: 1 - прямой старт, 2 - через перезапуск A-core
  int size;   // размер компонента
  int adr;    // адрес загрузки компонента в память
  int offset; // смещение до компонента от начала файла
  char* pbuf; // буфер для загрузки образа компонента
} blk[10];


#ifndef WIN32
unsigned char devname[50]="/dev/ttyUSB0";
#else
char devname[50]="";
DWORD bytes_written, bytes_read;
int port_no;
char port_name[256];
#endif

#ifndef WIN32
bzero(fileflag,sizeof(fileflag));
#else
memset(fileflag, 0, sizeof(fileflag));
#endif

while ((opt = getopt(argc, argv, "hp:ft:ms:bc")) != -1) {
  switch (opt) {
   case 'h': 
     
printf("\n Утилита предназначена для аварийной USB-загрузки устройств на чипете Balong V7\n\n\
%s [ключи] <имя файла для загрузки>\n\n\
 Допустимы следующие ключи:\n\n"
#ifndef WIN32
"-p <tty> - serial port for communication with the bootloader (by default /dev/ttyUSB0)\n"
#else
"-p # - number of serial port for communication with the loader (e.g. -p8)\n"
"  If the -p key is not specified, port auto-detection is performed"
#endif
"-f - load usbloader only before fastboot (without starting linux)\n\
-b - similar to -f, additionally disable checking of defective blocks during erasing\n\
-t <file> - fetch partition table from specified file\n\
-m - show boot loader partition table and terminate the work\n\
-s n - set file flag for partition n (the key can be specified several times)\n\
-c - do not perform automatic partition erase patch\n\
\n",argv[0]);
    return;

   case 'p':
    strcpy(devname,optarg);
    break;

   case 'f':
     fbflag=1;
     break;

   case 'c':
     cflag=1;
     break;

   case 'b':
     fbflag=1;
     bflag=1;
     break;

   case 'm':
     mflag=1;
     break;

   case 't':
     tflag=1;
     strcpy(ptfile,optarg);
     break;

   case 's':
     i=atoi(optarg);
     if (i>41) {
       printf("\n Section #%i doesn't exist",i);
       return;
     }
     fileflag[i]=1;
     break;
     
   case '?':
   case ':':  
     return;
    
  }
}  

printf("\n Balong-chipset Emergency USB Loader, version 2.20, (c) forth32, 2015");
#ifdef WIN32
printf("\n Port for Windows 32bit (c) rust3028, 2016");
#endif


if (optind>=argc) {
    printf("\n - No file name is specified for downloading/\n");
    return;
}  

ldr=fopen(argv[optind],"rb");
if (ldr == 0) {
  printf("\n Error opening %s",argv[optind]);
  return;
}

// Прверяем сигнатуру usloader
fread(&i,1,4,ldr);
if (i != 0x20000) {
  printf("\n File %s is not a usbloader loader",argv[optind]);
  return;
}  

fseek(ldr,36,SEEK_SET); // начало описателей блоков для загрузки

// Разбираем заголовок

fread(&blk[0],1,16,ldr);  // raminit
fread(&blk[1],1,16,ldr);  // usbldr

//---------------------------------------------------------------------
// Чтение компонентов в память
for(bl=0;bl<2;bl++) {

  // выделяем память под полный образ раздела
  blk[bl].pbuf=(char*)malloc(blk[bl].size);

  // читаем образ раздела в память
  fseek(ldr,blk[bl].offset,SEEK_SET);
  res=fread(blk[bl].pbuf,1,blk[bl].size,ldr);
  if (res != blk[bl].size) {
      printf("\n Unexpected end of file: read %i expected %i\n",res,blk[bl].size);
      return;
  }
  if (bl == 0) continue; // для raminit более ничего делать не надо

  //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%    
  // fastboot-патч
  if (fbflag) {
    koff=locate_kernel(blk[bl].pbuf,blk[bl].size);
    if (koff != 0) {
      blk[bl].pbuf[koff]=0x55; // патч сигнатуры
      blk[bl].size=koff+8; // обрезаем раздел до начала ядра
    }
    else {
        printf("\n There is no ANDROID component in the bootloader, so fastboot booting is not possible");
        exit(0);
    }    
  }  

  //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%    
  // Ищем таблицу разделов в загрузчике
  ptoff=find_ptable_ram(blk[bl].pbuf,blk[bl].size);
  
  //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%    
  // патч таблицы разделов
  if (tflag) {
    pt=fopen(ptfile,"rb");
    if (pt == 0) { 
      printf("\n Unable to find %s file - partition table replacement is not possible\n",ptfile);
      return;
    }  
    fread(ptbuf,1,2048,pt);
    fclose(pt);
    if (memcmp(headmagic,ptbuf,sizeof(headmagic)) != 0) {
      printf("\n The %s file is not a partition table",ptfile);
      return;
    }  
    if (ptoff == 0) {
          printf("\n Partition table not found in bootloader - cannot be replaced");
	  return;
    }
    memcpy(blk[bl].pbuf+ptoff,ptbuf,2048);
  }
  ptable=(struct ptable_t*)(blk[bl].pbuf+ptoff);
  
  //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%    
  // Патч файловых флагов
  for(i=0;i<41;i++) {
    if (fileflag[i]) {
      ptable->part[i].nproperty |= 1;
    }  
  }  

  //%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%    
  // Вывод таблицы разделов
  if (mflag) {
    if (ptoff == 0) {
      printf("\n Partition table not found - map output not possible\n");
      return;
    }
    show_map(*ptable);
    return;
  }

  // Патч erase-процедуры на предмет игнорировани бедблоков
  if (bflag) {
    res=perasebad(blk[bl].pbuf, blk[bl].size);
    if (res == 0) { 
      printf("\n! No isbad signature found - Unable to download");  
      return;
    }  
  }
  // Удаление процедуры flash_eraseall
  if (!cflag) {
      res = pv7r1(blk[bl].pbuf, blk[bl].size);
      if (res == 0)
          res = pv7r2(blk[bl].pbuf, blk[bl].size);
      if (res == 0)
          res = pv7r11(blk[bl].pbuf, blk[bl].size);
      if (res == 0)
          res = pv7r22(blk[bl].pbuf, blk[bl].size);
      if (res == 0)
          res = pv7r22_2(blk[bl].pbuf, blk[bl].size);
      if (res == 0)
          res = pv7r22_3(blk[bl].pbuf, blk[bl].size);
      if (res != 0)  printf("\n\n * Deleted flash_eraseall procedure at offset %08x", blk[bl].offset + res);
      else {
          printf("\n The eraseall procedure was not found in the loader - use the -s key to load without a patch!\n");
          return;
      }
  }

}

//---------------------------------------------------------------------

#ifdef WIN32
if (*devname == '\0')
{
  printf("\n\n Search for the emergency boot port...\n");
  
  if (find_port(&port_no, port_name) == 0)
  {
    sprintf(devname, "%d", port_no);
    printf("Port: \"%s\"\n", port_name);
  }
  else
  {
    printf("Port not detected! \n");
    return;
  }
}
#endif

if (!open_port(devname)) {
  printf("\n Serial port does not open\n");
  return;
}  


// Проверяем загрузочный порт
c=0;
#ifndef WIN32
write(siofd,"A",1);
res=read(siofd,&c,1);
#else
    WriteFile(hSerial, "A", 1, &bytes_written, NULL);
    FlushFileBuffers(hSerial);
    Sleep(100);
    ReadFile(hSerial, &c, 1, &bytes_read, NULL);
#endif
if (c != 0x55) {
  printf("\n ! The port is not in USB Boot mode\n");
  return;
}  

//----------------------------------
// главный цикл загрузки - загружаем все блоки, найденные в заголовке
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%    

printf("\n\n Component Address Size %% download\n------------------------------------------\n");

for(bl=0;bl<2;bl++) {

  datasize=1024;
  pktcount=1;


  // фрмируем пакет начала блока  
  *((unsigned int*)&cmdhead[4])=htonl(blk[bl].size);
  *((unsigned int*)&cmdhead[8])=htonl(blk[bl].adr);
  cmdhead[3]=blk[bl].lmode;
  
  // отправляем пакет начала блока
  res=sendcmd(cmdhead,14);
  if (!res) {
    printf("\nThe modem rejected the header packet\n");
    return;
  }  

  
  // ---------- Цикл поблочной загрузки данных ---------------------
  for(adr=0;adr<blk[bl].size;adr+=1024) {

    // формируем размер последнего загружаемого пакета
    if ((adr+1024)>=blk[bl].size) datasize=blk[bl].size-adr;  

    printf("\r %s    %08x %8i   %i%%",bl?"usbboot":"raminit",blk[bl].adr,blk[bl].size,(adr+datasize)*100/blk[bl].size); 
  
    // готовим пакет данных
    cmddata[1]=pktcount;
    cmddata[2]=(~pktcount)&0xff;
    memcpy(cmddata+3,blk[bl].pbuf+adr,datasize);
    
    pktcount++;
    if (!sendcmd(cmddata,datasize+5)) {
      printf("\nThe modem has rejected the data packet");
      return;
    }  
  }
  free(blk[bl].pbuf);

  // Фрмируем пакет конца данных
  cmdeod[1]=pktcount;
  cmdeod[2]=(~pktcount)&0xff;

  if (!sendcmd(cmdeod,5)) {
    printf("\nThe modem has rejected the end-of-data packet");
  }
printf("\n");  
} 
printf("\n Downloading is finished\n");  
}


