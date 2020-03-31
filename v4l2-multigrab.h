#if !defined(IO_READ) && !defined(IO_MMAP) && !defined(IO_USERPTR)
#define IO_READ
#define IO_MMAP
#define IO_USERPTR
#endif

#include <stdio.h>
#include <sstream>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <linux/videodev2.h>
#include <jpeglib.h>
#include <libv4l2.h>
#include <signal.h>
#include <stdint.h>
#include <inttypes.h>

#include "config.h"

using namespace std; 

#define CLEAR(x) memset (&(x), 0, sizeof (x))

#ifndef VERSION
#define VERSION "unknown"
#endif

#if defined(IO_MMAP) || defined(IO_USERPTR)

// minimum number of buffers to request in VIDIOC_REQBUFS call
#define VIDIOC_REQBUFS_COUNT 2
#endif

typedef enum {
				#ifdef IO_READ
        		IO_METHOD_READ,
				#endif
				#ifdef IO_MMAP
        		IO_METHOD_MMAP,
				#endif
				#ifdef IO_USERPTR
        		IO_METHOD_USERPTR,
				#endif
} io_method;



struct buffer {
        		void * start;
        		size_t length;
};


static io_method        io              = IO_METHOD_MMAP;

// global settings
static int continuous = 0;
static unsigned char jpegQuality = 100;
static char* jpegFilenamePart = NULL;

static const char* const continuousFilenameFmt = "%s_%010"PRIu32".jpg"; 

class Camera{

		int fd;
		struct buffer * buffers;
		unsigned int    n_buffers;
		
		char* jpegFilename;
		char* deviceName; 
		int idCam;
		int width;
		int height;
		int fps;
		struct v4l2_buffer buf;
		
		public:
			Camera(char*, char*, int , int, int, int);
			~Camera();

		public:

			void InstallSIGINTHandler();
			void errno_exit(const char*);
            int  xioctl(int, void*);
			
			void deviceOpen(char*);
			

			void readInit(unsigned int);
			void mmapInit(char*);
			void userptrInit(unsigned int,char*);
			void deviceInit(char*);
			void deviceUninit();
			
			void captureStart(void);		

            
            void yuv2rgb(int, int , unsigned char*, unsigned char*);
            void jpegWrite(unsigned char*, char*);

            unsigned char* imageProcess(char*);
            int frameRead(char*);
            int getFrame(char*);

            void captureStop();
            void deviceClose();
			
			

			
};


