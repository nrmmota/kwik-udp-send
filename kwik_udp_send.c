/*
Copyright by Michael Korneev 2015
*/
#define MULTICAST

#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#define TS_PACKET_SIZE 188	
#define PKT_ACCUMUL_NUM 10000
#define PKT_FULL_NUM    30000
char                *OneFile = NULL;
char                *dir = NULL;
unsigned char*      send_buf;
pthread_mutex_t     c_mutex = PTHREAD_MUTEX_INITIALIZER;
unsigned int        snd_pkt_num;       //the size of ts packets, that sended per one operation
unsigned int        packet_size;       //the size of udp packet, that sended per one operation
unsigned char       *cache_buf;        //the cache buffer
unsigned int        pkt_full;          //the quantity of all packets in the chache buffer
unsigned int        pkt_num = 0;       //the quanitity of filled packets in the chache buffer
unsigned int        start_pkt = 0;     //the number of the first packet in cache buffer where is the buffer is filled 
unsigned int        last_pkt = 0;      //the number of the last packet in cache buffer where is the buffer is filled 
unsigned int        file_fin = 0;      //file reading finished
unsigned int        bitrate = 0;
int                 bCacheReady = 0;
int                 bNoMoreFile = 0;
int                 transport_fd = 0;
int                 sockfd;
struct timespec     nano_sleep_packet;
struct timespec     nano_sleep_packet_r;
struct sockaddr_in  addr;
int                 bPrint = 0;
int                 bMsg = 0;
FILE                *LogFileD = NULL;
unsigned int        PktAccumulNum = PKT_ACCUMUL_NUM;
unsigned int        BufDelay = 0;
unsigned int        bAccumulOnZero = 0;
unsigned int        MinFileSize = 0;
int                 bDontExit = 0; 

int CheckIp(char *Value){
     int bWasChifr = 0;
     int NumOfChifr = 0;
     int DigNum = 0;
     int i;
     int len = strlen(Value);
     for(i=0; i<len; i++){
         if(Value[i] == '.'){
            if(bWasChifr == 0)return 0;
            bWasChifr = 0;
            NumOfChifr = 0;
            DigNum++;
            continue;
         }
         if(!(Value[i] >=0x30 && Value[i] <=0x39))return 0;
         NumOfChifr++;
         if(NumOfChifr > 3)return 0;
         bWasChifr = 1;
     }
     if(DigNum != 3)return 0;
     if(NumOfChifr > 3 || NumOfChifr == 0)return 0;
     return 1;
}
int CheckDecValue(char *Value, int bSize){
     int i;
     int len = strlen(Value);
     if(bSize == 1){
        if(Value[len-1] == 'M' || Value[len-1] == 'K')len--;
     }
     for(i=0; i<len; i++){
         if(!(Value[i] >=0x30 && Value[i] <=0x39))return 0;
     }
     return 1;
}
int ReadSizeInPkt(char *Value){
     char st[100];
     int len = strlen(Value);
     if(Value[len-1] == 'M' || Value[len-1] == 'K'){
        memcpy(st, Value, len - 1);
        int Val = atoi(st);
        if(Value[len-1] == 'M')return Val * 1024 * 1024 / TS_PACKET_SIZE;
        return Val * 1024 / TS_PACKET_SIZE;
     }
     return atoi(Value);     
}
int ReadSize(char *Value){
     char st[100];
     int len = strlen(Value);
     if(Value[len-1] == 'M' || Value[len-1] == 'K'){
        memcpy(st, Value, len - 1);
        int Val = atoi(st);
        if(Value[len-1] == 'M')return Val * 1024 * 1024;
        return Val * 1024;
     }
     return atoi(Value);     
}
void PrintMsg(char *msg){
     time_t    t;
     struct tm tm;
     if(bMsg == 1)printf("%s", msg);
     if(LogFileD != NULL){
        t = time(NULL);
        tm = *localtime(&t);
        fprintf(LogFileD, "[%d-%.2d-%.2d %.2d:%.2d:%.2d] %s", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, msg);
     }
}
void process_file(char *tsfile){
     char          st[100]; 
     unsigned char temp_buf[TS_PACKET_SIZE];
     int len;
     transport_fd = open(tsfile, O_RDONLY);
     printf("Processing: %s\r\n", tsfile);
     if(transport_fd < 0) { 
        fprintf(stderr, "couldn't open file: %s\n", tsfile);
        return;   
     }
     unsigned int read_bytes = 0;  //number of bytes that read from file
     for(;;){         
         if(pkt_num + 1 >= pkt_full){
            //the chache buffer is full
            if(bPrint == 1)
               PrintMsg("Cache is full\r\n");
            nanosleep(&nano_sleep_packet_r, 0);
            continue;
         }
         len = read(transport_fd, temp_buf, TS_PACKET_SIZE);
         if(len == 0 || len < TS_PACKET_SIZE){
            if(bDontExit == 1){
               close(transport_fd);             
               printf("Processed: %s\r\n", tsfile);
               return;                           
            }
            if(len < TS_PACKET_SIZE && len != 0)
              fprintf(stderr, "read < TS_PACKET_SIZE while reading: %d\n", len);                      
            //file reading completed
            close(transport_fd);             
            printf("Processed: %s\r\n", tsfile);
            //bCacheReady = 0;
            return;            
         
        }
         read_bytes += len;
         pthread_mutex_lock( &c_mutex );
         unsigned char *src_buf = cache_buf + last_pkt * TS_PACKET_SIZE;
         memcpy(src_buf, temp_buf, TS_PACKET_SIZE);
         last_pkt++;
         pkt_num++;
         if(bPrint == 1){
            sprintf(st, "reading file %d\n", pkt_num);
            PrintMsg(st);
         }
         if(last_pkt == pkt_full)last_pkt = 0;
         if(pkt_num > PktAccumulNum)bCacheReady = 1;
         pthread_mutex_unlock( &c_mutex );        
         if(pkt_num  > PktAccumulNum)
            nanosleep(&nano_sleep_packet_r, 0);
     }     
}
void *reading_file( void *ptr ){
     time_t          sv_time = 0;
     struct stat     statbuf;
     for(;;){
         stat(OneFile, &statbuf);
         if(MinFileSize != 0){
            if(statbuf.st_size < MinFileSize){
               nanosleep(&nano_sleep_packet_r, 0);
               continue;
            }
         }
         if(statbuf.st_mtime != sv_time)
            process_file(OneFile);
         sv_time = statbuf.st_mtime;
         sleep(1);
     }
}
void *reading_file2( void *ptr ){
     for(;;){
         if(MinFileSize != 0){
            struct stat st;
            stat(OneFile, &st);
            if(st.st_size < MinFileSize){
               nanosleep(&nano_sleep_packet_r, 0);
               continue;
            }
         }
         process_file(OneFile);
     }
}
void *reading_thread( void *ptr ){
     DIR            *FD;
     struct stat     statbuf;
     struct dirent  *in_file;
     struct tm      *tmd;
     time_t         min_time = 0xFFFFFFFF;          //minimal played time
     time_t         srch_min_time = 0;              //minimal time in the current iteration of directory scanning
     char   *dir_buf = malloc(strlen(dir) + 100);
     char   *cur_buf = malloc(strlen(dir) + 100);   //name buffer for the current file
     //Scanning the in directory 
     for(;;){
          FD = opendir (dir);
          if (NULL == FD){
             fprintf(stderr, "Panic : Failed to open input directory - %s\n", strerror(errno));
             sleep(1);
             continue;
          }
          while((in_file = readdir(FD))){
                if(strcmp(in_file->d_name, ".") == 0)continue;
                if(strcmp(in_file->d_name, "..") == 0)continue;        
                sprintf(dir_buf, "%s%s", dir, in_file->d_name);
                stat(dir_buf, &statbuf);
                if(statbuf.st_size < 100 * 1024)
                   continue;
                if(srch_min_time == 0 || (statbuf.st_mtime > srch_min_time && ((unsigned int)statbuf.st_mtime) < ((unsigned int)min_time))){
                   if(srch_min_time == 0)srch_min_time = statbuf.st_mtime - 5;
                   min_time = statbuf.st_mtime;                
                   strcpy(cur_buf, dir_buf);
                }
          }//while((in_file = readdir(FD)))
          closedir(FD);
          if(min_time == srch_min_time || min_time == 0xFFFFFFFF){
             bNoMoreFile = 1;
             printf("No files in directory\r\n");
             sleep(1);
             continue;
          }
          if(pkt_num < PktAccumulNum){
             //waiting of current file finish to avoid a break in a next file
             while(pkt_num > 0){
                   bNoMoreFile = 1;
                   nanosleep(&nano_sleep_packet_r, 0);
             }
          }
          bNoMoreFile = 0;
          //playing a file with minimal modified time
          process_file(cur_buf);
          srch_min_time = min_time;  
          min_time = 0xFFFFFFFF;
     }//for(;;)
}
long long int usecDiff(struct timespec* time_stop, struct timespec* time_start)
{
     long long int temp = 0;
     long long int utemp = 0;
		   
     if(time_stop && time_start){
	if(time_stop->tv_nsec >= time_start->tv_nsec){
	   utemp = time_stop->tv_nsec - time_start->tv_nsec;    
	   temp = time_stop->tv_sec - time_start->tv_sec;
        }else{
    	   utemp = time_stop->tv_nsec + 1000000000 - time_start->tv_nsec;       
	   temp = time_stop->tv_sec - 1 - time_start->tv_sec;
        }
        if(temp >= 0 && utemp >= 0){
	   temp = (temp * 1000000000) + utemp;
        }else{
	   fprintf(stderr, "start time %ld.%ld is after stop time %ld.%ld\n", time_start->tv_sec, time_start->tv_nsec, time_stop->tv_sec, time_stop->tv_nsec);
	   temp = -1;
        }
     }else{
	fprintf(stderr, "memory is garbaged?\n");
	temp = -1;
     }
     return temp / 1000;
}
void SendEmptyPacket(){
     int i;
     unsigned char *buf = send_buf;
     for(i=0; i<snd_pkt_num; i++){
         //memset(buf, 0x00, TS_PACKET_SIZE);
         buf[0] = 0x47;
         ((unsigned short*)(buf+1))[0] = 0x1fff;
         buf[1] = 0x1F;
         buf[2] = 0xFF;
         buf[3] = 0x10;
         buf += TS_PACKET_SIZE;
     }
     //printf("Sending empty packet\r\n");
     int sent = sendto(sockfd, send_buf, packet_size, 0, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
     if(sent <= 0)perror("send() in SendEmpty: error ");
}

void SendPacket(){
     char  Msg[150];
     unsigned long long int packet_size2 = 0;
     int i;
     int bShow;
     unsigned char *dst_buf = send_buf;
     for(i=0; i<snd_pkt_num; i++){
         pthread_mutex_lock( &c_mutex );
         unsigned char *src_buf = cache_buf + start_pkt * TS_PACKET_SIZE;
         memcpy(dst_buf, src_buf, TS_PACKET_SIZE);         
         if(bPrint == 1){
            sprintf(Msg, "Sending packet: %d of %d, start: %d, last: %d\r\n", start_pkt, pkt_num, start_pkt, last_pkt);
            PrintMsg(Msg);
         }
         packet_size2 += TS_PACKET_SIZE;
         pkt_num--;
         start_pkt++;
         if(start_pkt == pkt_full){
            start_pkt = 0;
         }         
         if(pkt_num == 0 && bAccumulOnZero == 1){
            bCacheReady = 0;
         }
         pthread_mutex_unlock( &c_mutex );
         if(pkt_num <= 0)break;
         dst_buf += TS_PACKET_SIZE;         
     }
     int sent = sendto(sockfd, send_buf, packet_size2, 0, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
     if(sent <= 0)perror("send() in SendPacket: error ");
}
void *buf_info_thread( void *ptr ){
     for(;;){
         printf("Buffer: %d of %d, start: %d, last: %d\r\n", pkt_num, pkt_full, start_pkt, last_pkt);
         sleep(BufDelay);
     }
}
void *sending_thread( void *ptr ){
     unsigned long long int packet_time = 0;
     unsigned long long int real_time = 0; 
     struct                 timespec time_start;
     struct timespec        time_stop;
     memset(&time_start, 0, sizeof(time_start));
     memset(&time_stop, 0, sizeof(time_stop));
     clock_gettime(CLOCK_MONOTONIC, &time_start);
     for(;;){      
         clock_gettime(CLOCK_MONOTONIC, &time_stop);
         real_time = usecDiff(&time_stop, &time_start);
         while(real_time * bitrate > packet_time * 1000000){               
               if((bCacheReady == 1 || bNoMoreFile == 1) && pkt_num > 0){
                  SendPacket();
                  packet_time += packet_size * 8;
                  continue;
               }                       
               SendEmptyPacket();
               packet_time += packet_size * 8;               
         }//while(real_time * bitrate > packet_time * 1000000)
         nanosleep(&nano_sleep_packet, 0);
     }//for(;;)
}
int main (int argc, char *argv[]) {     
     pthread_t              thread1, thread2, s_thread;
     pthread_attr_t         attr;
     pthread_attr_t         attr_d;
     struct sched_param     param;
     int                    policy = SCHED_FIFO;   
     int                    policy_d = SCHED_OTHER; 
     char                   *port = NULL;
     char                   *ip = NULL;
     char                   *br = NULL;
     char                   *ts_in_udp = NULL;
     int                    bMinOnAccumul = 0;
     int                    i;
     int                    rt;
     int                    is_multicast = 0;
     int                    option_ttl = 0;
     char                   start_addr[4];
     memset(&addr, 0, sizeof(addr));
     memset(&nano_sleep_packet, 0, sizeof(nano_sleep_packet));
     memset(&nano_sleep_packet_r, 0, sizeof(nano_sleep_packet_r));
     pkt_full = PKT_FULL_NUM;
     param.sched_priority = 50;

     for(i=1; i<argc; i++){
         if(strcmp(argv[i], "-d") ==0){
            //directory
            int d_len = strlen(argv[i+1]);
            dir = malloc(d_len+2);
            strcpy(dir, argv[i+1]);
            if(dir[d_len-1] != '/')dir[d_len] = '/';            
            i++; 
            continue;
         }
         if(strcmp(argv[i], "-f") ==0){
            //file
            OneFile = argv[i+1];
            i++; 
            continue;
         }
         if(strcmp(argv[i], "-i") ==0){
            //ip addr
            if(CheckIp(argv[i+1]) == 0){
               printf("incorrect ip address: %s\n", argv[i+1]);
               exit(0);
            }
            ip = argv[i+1]; 
            memcpy(start_addr, ip, 3);
            start_addr[3] = 0;
            is_multicast = atoi(start_addr);
            is_multicast = (is_multicast >= 224) || (is_multicast <= 239);
            i++; 
            continue;
         }
         if(strcmp(argv[i], "-p") ==0){
            //port
            if(CheckDecValue(argv[i+1], 0) == 0){
               printf("incorrect port number: %s\n", argv[i+1]);
               exit(0);
            }else port = argv[i+1]; 
            i++;
            continue;
         }
         if(strcmp(argv[i], "-b") ==0){
            //bitrate 
            if(CheckDecValue(argv[i+1], 0) == 0){
               printf("incorrect bitrate: %s\n", argv[i+1]);
            }else br = argv[i+1]; 
            i++;
            continue;
         }
         if(strcmp(argv[i], "-m") == 0){
            //print messages to screen
            bMsg = 1; bPrint = 1; continue;
         }
         if(strcmp(argv[i], "-l") == 0){
            //print messages to a log file, the name of log follows -l
            LogFileD = fopen(argv[i+1], "a");  
            if(LogFileD == NULL){
               printf("couldn't open the log file %s\n", argv[i+1]);
            }else bPrint = 1;
            i++; 
            continue;
         }
         if(strcmp(argv[i], "--ts_in_udp") ==0 || strcmp(argv[i], "-u") == 0){
            //number of ts packets in one udp packet
            if(CheckDecValue(argv[i+1], 0) == 0){
               printf("incorrect ts_in_udp value: %s\n", argv[i+1]);
            }else ts_in_udp = argv[i+1]; 
            i++;
            continue;
         }
         if(strcmp(argv[i], "--ts_in_cache") ==0 || strcmp(argv[i], "-s") == 0){
            //number of ts packets in cache
            if(CheckDecValue(argv[i+1], 1) == 0){
               printf("incorrect ts_in_cache value: %s\n", argv[i+1]);
            }else pkt_full = ReadSizeInPkt(argv[i+1]); 
            printf("Packets in cache buffer: %d\n", pkt_full);
            i++;
            continue;
         }
         if(strcmp(argv[i], "--accumul_ts") ==0 || strcmp(argv[i], "-a") == 0){
            //number of ts packets in cache
            if(CheckDecValue(argv[i+1], 1) == 0){
               printf("incorrect accumul_ts value: %s\n", argv[i+1]);
            }else PktAccumulNum = ReadSizeInPkt(argv[i+1]); 
            printf("Accumul packets: %d\n", PktAccumulNum);
            i++;
            continue;
         }
         if(strcmp(argv[i], "--ttl") ==0 || strcmp(argv[i], "-t") == 0){
            //number of ts packets in cache
            if(CheckDecValue(argv[i+1], 0) == 0){
               printf("incorrect ts_in_cache value: %s\n", argv[i+1]);
            }else option_ttl = atoi(argv[i+1]); 
            i++;
            continue;
         }
         if(strcmp(argv[i], "--pri") ==0 || strcmp(argv[i], "-P") == 0){
            //number of ts packets in cache
            if(CheckDecValue(argv[i+1], 0) == 0){
               printf("incorrect ts_in_cache value: %s\n", argv[i+1]);
            }else param.sched_priority = atoi(argv[i+1]); 
            i++;
            continue;
         }       
         if(strcmp(argv[i], "-c") == 0){
            //don't exit on file reading (for FIFO)
            bDontExit = 1;
            continue;
         }   
         if(strcmp(argv[i], "-A") == 0){
            //waiting for packet accumultion on zero reach
            bAccumulOnZero = 1;
            continue;
         }  
         if(strcmp(argv[i], "-D") ==0){
            //show the buffer condition in some time(seconds)
            if(CheckDecValue(argv[i+1], 0) == 0){
               printf("incorrect delay value: %s\n", argv[i+1]);
            }else BufDelay = atoi(argv[i+1]);         
            i++; 
            continue;
         }
         if(strcmp(argv[i], "-F") ==0){
            //set minimal file size
            if(CheckDecValue(argv[i+1], 1) == 0){
               printf("incorrect file size: %s\n", argv[i+1]);
            }else MinFileSize = ReadSize(argv[i+1]);    
            printf("Minimal file size in bytes: %d\n", MinFileSize);     
            i++; 
            continue;
         }
         if(strcmp(argv[i], "-M") ==0){
            //set minimal file size, based on accumulation buffer value
            bMinOnAccumul = 1;    
            i++; 
            continue;
         }
     }
     if(bMinOnAccumul == 1){
        MinFileSize = PktAccumulNum * TS_PACKET_SIZE + (((double)PktAccumulNum * TS_PACKET_SIZE) * 0.25);
        printf("Minimal file size in bytes: %d\n", MinFileSize); 
     }
     if((dir == NULL && OneFile == NULL) || ip == NULL || port == NULL || (dir != NULL && OneFile != NULL)){
	fprintf(stderr, "Incorrect paramets, see help\n");
        if(LogFileD != NULL)fclose(LogFileD);
	return 0;
     }     
     addr.sin_family = AF_INET;
     addr.sin_addr.s_addr = inet_addr(ip);
     addr.sin_port = htons(atoi(port));
     if(br != NULL)bitrate = atoi(br);
     if(bitrate <= 0){
	bitrate = 100000000;
     } 
     if(ts_in_udp != NULL){
        snd_pkt_num = atoi(ts_in_udp);
        packet_size = snd_pkt_num * TS_PACKET_SIZE;
     }else{
        packet_size = 7 * TS_PACKET_SIZE;
        snd_pkt_num = 7;	
     }

     sockfd = socket(AF_INET, SOCK_DGRAM, 0);
     if(sockfd < 0){
	perror("socket(): error ");
        if(LogFileD != NULL)fclose(LogFileD);
	return 0;
     }     
     if(option_ttl != 0){
        if(is_multicast){
           rt = setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_TTL, &option_ttl, sizeof(option_ttl));
        }else{
           rt = setsockopt(sockfd, IPPROTO_IP, IP_TTL, &option_ttl, sizeof(option_ttl));
        }	
        if(rt < 0){
           perror("ttl configuration fail");
        }
	    
     }

     rt = pthread_setschedparam(pthread_self(), policy, &param);
     if(rt != 0)
        perror("pthread_setschedparam");
     rt = pthread_attr_init(&attr);
     if(rt != 0)
        perror("pthread_attr_init");
     rt = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);  
     if(rt != 0)
        perror("pthread_attr_setinheritsched");
     rt = pthread_attr_setschedpolicy(&attr, policy);
     if(rt != 0)
        perror("pthread_attr_setschedpolicy");


     cache_buf = malloc(pkt_full * TS_PACKET_SIZE);
     send_buf = malloc(packet_size);
     
     if(dir != NULL){
        rt = pthread_create( &thread1, &attr, reading_thread, (void*) 2);
        if(rt){
           fprintf(stderr,"Error - pthread_create(reading_thread) return code: %d\n",rt);
           if(LogFileD != NULL)fclose(LogFileD);
           exit(EXIT_FAILURE);
        }
     }
     if(OneFile != NULL){
        if(bDontExit == 1)rt = pthread_create( &thread1, &attr, reading_file2, (void*) 2);
        else{
           rt = pthread_create( &thread1, &attr, reading_file, (void*) 2);
        }
        if(rt){
           fprintf(stderr,"Error - pthread_create(reading_file) return code: %d\n",rt);
           if(LogFileD != NULL)fclose(LogFileD);
           exit(EXIT_FAILURE);
        }
     }
     if(BufDelay != 0){
        pthread_attr_init(&attr_d);
        pthread_attr_setinheritsched(&attr_d, PTHREAD_EXPLICIT_SCHED);       
        rt = pthread_create( &thread1, &attr_d, buf_info_thread, (void*) 2);
        if(rt){
           fprintf(stderr,"Error - pthread_create(buf_info_thread) return code: %d\n",rt);
           if(LogFileD != NULL)fclose(LogFileD);
           exit(EXIT_FAILURE);
        }
     }
     int   bFirst = 1;
     nano_sleep_packet.tv_nsec = 665778; // 1 packet at 100mbps
     nano_sleep_packet_r.tv_nsec = 665778; // 1 packet at 100mbps
     rt = pthread_create( &s_thread, &attr, sending_thread, (void*) 2);
     if(rt){
        fprintf(stderr,"Error - pthread_create(reading_file) return code: %d\n",rt);
        if(LogFileD != NULL)fclose(LogFileD);
        exit(EXIT_FAILURE);
     }
     for(;;){
         sleep(10);
     }
     
     close(sockfd);
     free(send_buf);
     if(LogFileD != NULL)fclose(LogFileD);
     pthread_attr_destroy(&attr);
     return 0; 
}

