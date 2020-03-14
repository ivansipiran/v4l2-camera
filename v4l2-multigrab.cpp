
#include "v4l2-multigrab.h"
using namespace std; 

#define Min(X, Y) (((X) < (Y)) ? (X) : (Y))
#define Max(X, Y) (((X) > (Y)) ? (X) : (Y))

/*****************************************************************************/
Camera::Camera(char* vidFilename, char* devCam, int idcam, int ancho, int alto, int ratefps){

		jpegFilename = vidFilename;
		deviceName = devCam; //"/dev/video0";
		idCam      = idcam;
		width      = ancho;
		height     = alto;
		fps         = ratefps;

		deviceOpen(deviceName);
		deviceInit(deviceName);

}

Camera::~Camera(){

	// close devices
	deviceUninit();
	deviceClose();


}

/**************************************************************************************************/
/**
SIGINT interput handler
*/
void StopContCapture(int sig_id) {
	printf("stoping continuous capture\n");
	continuous = 0;
}

void Camera::InstallSIGINTHandler() {
	struct sigaction sa;
	CLEAR(sa);
	
	sa.sa_handler = StopContCapture;
	if(sigaction(SIGINT, &sa, 0) != 0)
	{
		fprintf(stderr,"could not install SIGINT handler, continuous capture disabled");
		continuous = 0;
	}
}

/**
	Print error message and terminate programm with EXIT_FAILURE return code.
	\param s error message to print
*/
void Camera::errno_exit(const char* s)
{
	fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
	exit(EXIT_FAILURE);
}

/**
	Do ioctl and retry if error was EINTR ("A signal was caught during the ioctl() operation."). Parameters are the same as on ioctl.

	\param fd file descriptor
	\param request request
	\param argp argument
	\returns result from ioctl
*/
int Camera::xioctl(int request, void* argp)
{
	int r;

	do r = v4l2_ioctl(fd, request, argp);
	while (-1 == r && EINTR == errno);

	return r;
}


/************************************************************************************************/

void Camera::deviceOpen(char* deviceName)
{
	struct stat st;

	// stat file
	if (-1 == stat(deviceName, &st)) {
		fprintf(stderr, "Cannot identify '%s': %d, %s\n", deviceName, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// check if its device
	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "%s is no device\n", deviceName);
		exit(EXIT_FAILURE);
	}

	// open device
	fd = v4l2_open(deviceName, O_RDWR /* required */ | O_NONBLOCK, 0);

	// check if opening was successfull
	if (-1 == fd) {
		fprintf(stderr, "Cannot open '%s': %d, %s\n", deviceName, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}


	
}



#ifdef IO_READ
void Camera::readInit(unsigned int buffer_size)
{
	buffers = (buffer*) calloc(1, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	buffers[0].length = buffer_size;
	buffers[0].start = malloc(buffer_size);

	if (!buffers[0].start) {
		fprintf (stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}
}
#endif

#ifdef IO_MMAP
void Camera::mmapInit(char* deviceName)
{
	struct v4l2_requestbuffers req;

	CLEAR(req);

	req.count = VIDIOC_REQBUFS_COUNT;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	//xioctl => Memory mapped buffers are located in device memory and must be allocated with this ioctl before they can be mapped
	// into the application’s address space. User buffers are allocated by applications themselves, and this ioctl is merely used to switch the driver into user pointer I/O mode
	// and to setup some internal structures
	
	if (-1 == xioctl(VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s does not support memory mapping\n", deviceName);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_REQBUFS");
		}
	}

	printf("nro buffers %d",req.count);

	if (req.count < 2) {
		fprintf(stderr, "Insufficient buffer memory on %s\n", deviceName);
		exit(EXIT_FAILURE);
	}

	buffers = (buffer*)calloc(req.count, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		struct v4l2_buffer buf;

		CLEAR(buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers;

		if (-1 == xioctl(VIDIOC_QUERYBUF, &buf))
			errno_exit("VIDIOC_QUERYBUF");

		buffers[n_buffers].length = buf.length;
		buffers[n_buffers].start = v4l2_mmap(NULL /* start anywhere */, buf.length, PROT_READ | PROT_WRITE /* required */, MAP_SHARED /* recommended */, fd, buf.m.offset);

		if (MAP_FAILED == buffers[n_buffers].start)
			errno_exit("mmap");
	}
}
#endif

#ifdef IO_USERPTR
void Camera::userptrInit(unsigned int buffer_size, char* deviceName)
{
	struct v4l2_requestbuffers req;
	unsigned int page_size;

	page_size = getpagesize();
	buffer_size = (buffer_size + page_size - 1) & ~(page_size - 1);

	CLEAR(req);

	req.count = VIDIOC_REQBUFS_COUNT;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_USERPTR;

	if (-1 == xioctl(VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s does not support user pointer i/o\n", deviceName);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_REQBUFS");
		}
	}

	buffers = (buffer*)calloc(4, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\n");
		exit(EXIT_FAILURE);
	}

	for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
		buffers[n_buffers].length = buffer_size;
		buffers[n_buffers].start = memalign(/* boundary */ page_size, buffer_size);

		if (!buffers[n_buffers].start) {
			fprintf(stderr, "Out of memory\n");
			exit(EXIT_FAILURE);
		}
	}
}
#endif

/**
	initialize device
*/
void Camera::deviceInit(char* deviceName)
{
	printf("...deviceInit \n");

	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_format fmt;
	struct v4l2_streamparm frameint;
	unsigned int min;

	if (-1 == xioctl(VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s is no V4L2 device\n",deviceName);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) { //V4L2_CAP_VIDEO_CAPTURE : The device supports the single-planar API through the Video Capture interface.
		fprintf(stderr, "%s is no video capture device\n",deviceName);
		exit(EXIT_FAILURE);
	}

	switch (io) {
					#ifdef IO_READ
					case IO_METHOD_READ:
										if (!(cap.capabilities & V4L2_CAP_READWRITE)) { //V4L2_CAP_READWRITE : The device supports the read() and/or write() I/O methods.
										fprintf(stderr, "%s does not support read i/o\n",deviceName);
										exit(EXIT_FAILURE);
										}
										break;
				
					#endif

					#ifdef IO_MMAP
					case IO_METHOD_MMAP:
					#endif

					
					#ifdef IO_USERPTR
					case IO_METHOD_USERPTR:
					#endif
					
					#if defined(IO_MMAP) || defined(IO_USERPTR)
      							if (!(cap.capabilities & V4L2_CAP_STREAMING)) {//V4L2_CAP_STREAMING : The device supports the streaming I/O method.
									fprintf(stderr, "%s does not support streaming i/o\n",deviceName);
									exit(EXIT_FAILURE);
								}
								break;
					#endif
	}// from switch


	/* Select video input, video standard and tune here. */
	CLEAR(cropcap);

	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;//Applications use this function to query the cropping limits, the pixel aspect of images and to calculate scale factors. the field type of v4l2_Cropcap 
	// save the type of buffer.

	if (0 == xioctl(VIDIOC_CROPCAP, &cropcap)) {
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect; // Default cropping rectangle, it shall cover the “whole picture”. Assuming pixel aspect 1/1 this could be for example a 640 × 480 rectangle for NTSC

		if (-1 == xioctl(VIDIOC_S_CROP, &crop)) {
			switch (errno) {
				case EINVAL:
					/* Cropping not supported. */
					break;
				default:
					/* Errors ignored. */
					break;
			}
		}
	} else {
		/* Errors ignored. */
	}

	CLEAR(fmt);

	// v4l2_format => the field type of v4l2_format save the type of buffer and other configurations

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = width;
	fmt.fmt.pix.height = height;
	fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;

	if (-1 == xioctl(VIDIOC_S_FMT, &fmt))
		errno_exit("VIDIOC_S_FMT");

	if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUV420) {
		fprintf(stderr,"Libv4l didn't accept YUV420 format. Can't proceed.\n");
		exit(EXIT_FAILURE);
	}

	/* Note VIDIOC_S_FMT may change width and height. */
	if (width != fmt.fmt.pix.width) {
		width = fmt.fmt.pix.width;
		fprintf(stderr,"Image width set to %i by device %s.\n", width, deviceName);
	}

	if (height != fmt.fmt.pix.height) {
		height = fmt.fmt.pix.height;
		fprintf(stderr,"Image height set to %i by device %s.\n", height, deviceName);
	}
	
  /* If the user has set the fps to -1, don't try to set the frame interval */
  if (fps != -1)
  {
    CLEAR(frameint);
    
    /* Attempt to set the frame interval. */
    frameint.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    frameint.parm.capture.timeperframe.numerator = 1;
    frameint.parm.capture.timeperframe.denominator = fps;
    if (-1 == xioctl(VIDIOC_S_PARM, &frameint))
      fprintf(stderr,"Unable to set frame interval.\n");
  }

	/* Buggy driver paranoia. */
	min = fmt.fmt.pix.width * 2;

	if (fmt.fmt.pix.bytesperline < min)
		fmt.fmt.pix.bytesperline = min;
	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	if (fmt.fmt.pix.sizeimage < min)
		fmt.fmt.pix.sizeimage = min;

	switch (io) {
#ifdef IO_READ
		case IO_METHOD_READ:
			readInit(fmt.fmt.pix.sizeimage);
			break;
#endif

#ifdef IO_MMAP
		case IO_METHOD_MMAP:
			mmapInit(deviceName);
			break;
#endif

#ifdef IO_USERPTR
		case IO_METHOD_USERPTR:
			userptrInit(fmt.fmt.pix.sizeimage,deviceName);
			break;
#endif
	}
}


// Libera los buffers en cada tipo de lectura. Por defecto usamos el método MMAP, libera con munmap
 void Camera::deviceUninit(void)
{
	
	unsigned int i;

	switch (io) {
					#ifdef IO_READ
					case IO_METHOD_READ:
										free(buffers[0].start);
										break;
					#endif

					
					#ifdef IO_MMAP
					case IO_METHOD_MMAP:
										for (i = 0; i < n_buffers; ++i)
											if (-1 == v4l2_munmap(buffers[i].start, buffers[i].length))//Unmap device memory
												errno_exit("munmap");
										break;
					#endif

					

					#ifdef IO_USERPTR
					case IO_METHOD_USERPTR:
										for (i = 0; i < n_buffers; ++i)
											free(buffers[i].start);
										break;
					#endif
	}

	free(buffers);
}


// start capturing

void Camera::captureStart(void)
{

	
	unsigned int i;
	enum v4l2_buf_type type;// buffer structure acts as a container for the planes. 
	//Only pointers to buffers (planes) are exchanged, the data itself is not copied.
	// These pointers, together with meta-information like timestamps or field parity are stores in struct v4l2_buffer

	switch (io) {
					#ifdef IO_READ
					case IO_METHOD_READ:
										/* Nothing to do. */
		    							printf("IO_READ \n");
										break;
					#endif

			
					#ifdef IO_MMAP
					case IO_METHOD_MMAP:
										printf("MMAP RGB\n");
										for (i = 0; i < n_buffers; ++i) {
												struct v4l2_buffer buf;

												CLEAR(buf);

												buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;//Type of the buffer,
												buf.memory = V4L2_MEMORY_MMAP;//This field must be set by applications and/or drivers in accordance with the selected I/O method
												buf.index = i;//Number of the buffer

										if (-1 == xioctl(VIDIOC_QBUF, &buf))
												errno_exit("VIDIOC_QBUF..aca");
										}

										type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

										if (-1 == xioctl(VIDIOC_STREAMON, &type))
												errno_exit("VIDIOC_STREAMON");

												break;
					#endif

					

					#ifdef IO_USERPTR
					case IO_METHOD_USERPTR:
											printf("PUSERPTR\n");
											for (i = 0; i < n_buffers; ++i) {
												struct v4l2_buffer buf;

												CLEAR (buf);

												buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
												buf.memory = V4L2_MEMORY_USERPTR;
												buf.index = i;
												buf.m.userptr = (unsigned long) buffers[i].start;
												buf.length = buffers[i].length;

										if (-1 == xioctl(VIDIOC_QBUF, &buf))
												errno_exit("VIDIOC_QBUF");
										}

										type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

										if (-1 == xioctl(VIDIOC_STREAMON, &type))
												errno_exit("VIDIOC_STREAMON");

												break;
					#endif
	}
}


void Camera::yuv2rgb(int width, int height, unsigned char* src, unsigned char* dst) {
	int line, column;
	unsigned char *py, *pu, *pv, *r, *g, *b;
	unsigned char *tmp = dst;
	
	double aux_py, aux_pu, aux_pv;
	int chr, chg, chb;	

	// In this format each four bytes is two pixels. Each four bytes is two Y's, a Cb and a Cr.
	// Each Y goes to one of the pixels, and the Cb and Cr belong to both pixels.
	unsigned char *base_py = src;
	unsigned char *base_pu = src+(height*width);
	unsigned char *base_pv = src+(height*width)+(height*width)/4;

	for (line = 0; line < height; ++line) {
		for (column = 0; column < width; ++column) {


			py = base_py+(line*width)+column;
			pu = base_pu+(line/2*width/2)+column/2;
			pv = base_pv+(line/2*width/2)+column/2;
		

                        aux_py = (double) *py;
                        aux_pu = (double) *pu;
                        aux_pv = (double) *pv;

			
			// Hay que restar 128(imagen de 8 bits) de las componentes 
			chr = (aux_py) + (aux_pv-128) * 1.402;
			chg = (aux_py) - (aux_pu-128) * 0.344  - (aux_pv-128) * 0.714;
			chb = (aux_py) + (aux_pu-128) * 1.772;

	
			// clamp de valores de 0 a 255 del rgb
 			chr = Max(0, Min(255, chr));
			chg = Max(0, Min(255, chg));
			chb = Max(0, Min(255, chb));

			//printf("clamp\n");
				
			*tmp++ = (unsigned char) chr;
			*tmp++ = (unsigned char) chg;
			*tmp++ = (unsigned char) chb;

			//printf("PUNTEROS RGB\n");
		}
	}
}



/**
	Write image to jpeg file.

	\param img image to write
*/
void Camera::jpegWrite(unsigned char* img, char* jpegFilename)
{

	
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;

	
	JSAMPROW row_pointer[1];
	
	string jpegFilename1(jpegFilename);
	jpegFilename1 += ".jpeg";
	cout<< jpegFilename1 <<endl;

	
	FILE *outfile = fopen(jpegFilename1.c_str(), "wb" );

	// try to open file for saving
	if (!outfile) {
		errno_exit("The handler of the file is out range");
	}

	
	// create jpeg data
	cinfo.err = jpeg_std_error( &jerr );
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, outfile);

	
	// set image parameters
	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;

	// set jpeg compression parameters to default
	
	jpeg_set_defaults(&cinfo);
	// and then adjust quality setting
	
	jpeg_set_quality(&cinfo, jpegQuality, TRUE);

	// start compress
	
	jpeg_start_compress(&cinfo, TRUE);

	// feed data
	while (cinfo.next_scanline < cinfo.image_height) {
		row_pointer[0] = &img[cinfo.next_scanline * cinfo.image_width *  cinfo.input_components];
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	// finish compression
	jpeg_finish_compress(&cinfo);
	// destroy jpeg data
	jpeg_destroy_compress(&cinfo);
	
	// close output file
	fclose(outfile);
	printf("fclose.. \n");
}

/**
	process image read
*/
void Camera::imageProcess( char* jpegFilename)
{
	//timestamp.tv_sec
	//timestamp.tv_usec
	printf("P1 \n");

	const void* p = buffers[buf.index].start;
	unsigned char* src = (unsigned char*)p;
	unsigned char* dst = (unsigned char*) malloc(width*height*3*sizeof(char));

	// This function it was modified to convert yuv to rgb (Y ranges from 16-235 corresponding to 0-1 brightness,U and V ranges from 16-240 corresponding -0.5-0.5, where 128 is zero)
	yuv2rgb(width, height, src, dst);
	printf("P2 \n");

	stringstream cc;
	cc<<jpegFilename;
	
	// write jpeg
	jpegWrite(dst,cc.str().c_str());
	
	// free temporary image
	free(dst);
}

/**
	read single frame
*/
int Camera::frameRead(char* jpegFilename)
{
		
#ifdef IO_USERPTR
	unsigned int i;
#endif

	switch (io) {
#ifdef IO_READ
		case IO_METHOD_READ:
		    //printf("READ \n");
			if (-1 == v4l2_read(fd, buffers[0].start, buffers[0].length)) {
				switch (errno) {
					case EAGAIN:
						return 0;

					case EIO:
						// Could ignore EIO, see spec.
						// fall through

					default:
						errno_exit("read");
				}
			}

			struct timespec ts;
			struct timeval timestamp;
			clock_gettime(CLOCK_MONOTONIC,&ts);
			timestamp.tv_sec = ts.tv_sec;
			timestamp.tv_usec = ts.tv_nsec/1000;
			
			break;
#endif

#ifdef IO_MMAP
		case IO_METHOD_MMAP:
			printf("MMAP \n");
			CLEAR(buf);

			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;

			if (-1 == xioctl(VIDIOC_DQBUF, &buf)) {
				switch (errno) {
					case EAGAIN:
						return 0;

					case EIO:
						// Could ignore EIO, see spec
						// fall through

					default:
						errno_exit("VIDIOC_DQBUF");
				}
			}

			assert(buf.index < n_buffers);

			if (-1 == xioctl(VIDIOC_QBUF, &buf))
				errno_exit("VIDIOC_QBUF");

			break;
#endif

#ifdef IO_USERPTR
			case IO_METHOD_USERPTR:
				printf("USERPTR \n");
				CLEAR (buf);

				buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				buf.memory = V4L2_MEMORY_USERPTR;

				if (-1 == xioctl(VIDIOC_DQBUF, &buf)) {
					switch (errno) {
						case EAGAIN:
							return 0;

						case EIO:
							// Could ignore EIO, see spec.
							// fall through

						default:
							errno_exit("VIDIOC_DQBUF");
					}
				}

				for (i = 0; i < n_buffers; ++i)
					if (buf.m.userptr == (unsigned long)buffers[i].start && buf.length == buffers[i].length)
						break;

				assert (i < n_buffers);
				
				if (-1 == xioctl(VIDIOC_QBUF, &buf))
					errno_exit("VIDIOC_QBUF");
				break;
#endif
	}

	return 1;
}


int Camera::getFrame(char* jpegFilename){


	unsigned int numberOfTimeouts;
	
	int ban = 0;
	
		for(;;){
			
				fd_set fds;
				struct timeval tv;
				int r;

				FD_ZERO(&fds);
				FD_SET(fd, &fds);

				/* Timeout. */
				tv.tv_sec = 1;
				tv.tv_usec = 0;

				r = select(fd + 1, &fds, NULL, NULL, &tv);
				

				if (-1 == r) {
					if (EINTR == errno)
						continue;

					errno_exit("select");
				}

				ban = frameRead(jpegFilename);

				

				if(ban==1){
										
					break;

				}

		}

		return ban;
}


/**
	stop capturing
*/
void Camera::captureStop(void)
{
	enum v4l2_buf_type type;

	switch (io) {
#ifdef IO_READ
		case IO_METHOD_READ:
			/* Nothing to do. */
			break;
#endif

#ifdef IO_MMAP
		case IO_METHOD_MMAP:
#endif
#ifdef IO_USERPTR
		case IO_METHOD_USERPTR:
#endif
#if defined(IO_MMAP) || defined(IO_USERPTR)
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

			if (-1 == xioctl(VIDIOC_STREAMOFF, &type))
			errno_exit("VIDIOC_STREAMOFF");

			break;
#endif
	}
}


/**
	close device
*/
void Camera::deviceClose(void)
{
	if (-1 == v4l2_close(fd))
		errno_exit("close");

	fd = -1;
}

/**
	print usage information
*/
/*void Camera::usage(FILE* fp, int argc, char** argv)
{
	fprintf(fp,
		"Usage: %s [options]\n\n"
		"Options:\n"
		"-d | --device name   Video device name [/dev/video0]\n"
		"-h | --help          Print this message\n"
		"-o | --output        Set JPEG output filename\n"
		"-q | --quality       Set JPEG quality (0-100)\n"
		"-m | --mmap          Use memory mapped buffers\n"
		"-r | --read          Use read() calls\n"
		"-u | --userptr       Use application allocated buffers\n"
		"-W | --width         Set image width\n"
		"-H | --height        Set image height\n"
		"-I | --interval      Set frame interval (fps) (-1 to skip)\n"
		"-c | --continuous    Do continous capture, stop with SIGINT.\n"
		"-v | --version       Print version\n"
		"",
		argv[0]);
	}

static const char short_options [] = "d:ho:q:mruW:H:I:vc";

static const struct option
long_options [] = {
	{ "device",     required_argument,      NULL,           'd' },
	{ "help",       no_argument,            NULL,           'h' },
	{ "output",     required_argument,      NULL,           'o' },
	{ "quality",    required_argument,      NULL,           'q' },
	{ "mmap",       no_argument,            NULL,           'm' },
	{ "read",       no_argument,            NULL,           'r' },
	{ "userptr",    no_argument,            NULL,           'u' },
	{ "width",      required_argument,      NULL,           'W' },
	{ "height",     required_argument,      NULL,           'H' },
	{ "interval",   required_argument,      NULL,           'I' },
	{ "version",	no_argument,		NULL,		'v' },
	{ "continuous",	no_argument,		NULL,		'c' },
	{ 0, 0, 0, 0 }
};*/

int main(int argc, char **argv)
{

	int fd1 = -1;
    int fd2 = -1;

	char* devCam1 = "/dev/video0";
	
	char* filename1 = "vid_cam1";
	
	int idcam1, idcam2;
	idcam1 = 1;
	
	unsigned int ancho1, ancho2;
	ancho1 = 640;
	ancho2 = 640;

	unsigned int alto1, alto2;
	alto1 = 480;
	alto2 = 480;

	unsigned int ratefps1, ratefps2;
	ratefps1 = 120;
	

	printf("Creando e inicializando objetos camera...\n");

	Camera cam1(filename1,devCam1, 1, ancho1, alto1, ratefps1);
	
	printf("Iniciando Captura...\n");
	cam1.captureStart();
		
	int ban1 = 1;
	int ban2 = 1;
	int i = 0;		

	char* name1, name2;
	
		for(;;){
		
			i++; 
			cout<<"Iter::"<<i<<endl;
			cout<<"_________";

			stringstream ss1;
						
			ss1<<filename1<<"_"<<i;
			
			ban1 = cam1.getFrame(ss1.str().c_str());

			if(ban1==0){

				printf("Error al leer \n");
				break;
			}

			cam1.imageProcess(ss1.str().c_str());
		
			//if (cin.get() == 's')
        	//	break;				
			
		
		}// from for


	printf("Capture Stop..\n");
	// stop capturing
	cam1.captureStop();


	/*if(jpegFilenamePart != 0){ 
		free(jpegFilename);
	}*/

	exit(EXIT_SUCCESS);

	return 0;
}
