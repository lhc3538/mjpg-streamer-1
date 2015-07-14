
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define uchar unsigned char 
#define uint unsigned int



typedef struct SMOTION{
	uchar *background;//存储单通道的灰度图
	uchar *motion;//运动图，某个像素值不为0说明是运动部分
	uchar *binpic;//二值化图像
	uchar noise;//噪声值
	uchar flag;//图像是否变化的标志
	uint  threshold;//阈值
	uint  width;
	uint  height;
	uint  timecount;//运动保持计时器
}SMOTION;

#define MAX_TIMECOUNT 10
#define BRIGHTNESS      200 //亮度调整值

static int  MAX_NOISE=20;  //背景噪声值
static int  MAX_THRESHOLD=100;
static unsigned char *gBuf;//该buf平均分三段，SMOTION结构中的background，motion，motion动态指向某一段
static SMOTION motion;
static int flag_init = -1;//初始化标志 -1未初始化 other 初始化

#define MAX2(x, y) ((x) > (y) ? (x) : (y))
#define MAX3(x, y, z) ((x) > (y) ? ((x) > (z) ? (x) : (z)) : ((y) > (z) ? (y) : (z)))
#define NORM               100
#define ABS(x)             ((x) < 0 ? -(x) : (x))
#define DIFF(x, y)         (ABS((x) - (y)))
#define NDIFF(x, y)        (ABS(x) * NORM/(ABS(x) + 2 * DIFF(x,y)))

/*yuv4:2:2格式转换为rgb24格式*/
int convert_yuv_to_rgb_pixel(int y, int u, int v)
{
	uint pixel32 = 0;
	uchar *pixel = (uchar *)&pixel32;
	int r, g, b;
	r = y + (1.370705 * (v-128));
	g = y - (0.698001 * (v-128)) - (0.337633 * (u-128));
	b = y + (1.732446 * (u-128));
	if(r > 255) r = 255;
	if(g > 255) g = 255;
	if(b > 255) b = 255;
	if(r < 0) r = 0;
	if(g < 0) g = 0;
	if(b < 0) b = 0;
	pixel[0] = r * 220 / 256;
	pixel[1] = g * 220 / 256;
	pixel[2] = b * 220 / 256;
	
	return pixel32;
}

int convert_yuv_to_rgb_buffer(uchar *yuv, uchar *rgb, uint width,uint height)
{
	uint in, out = 0,cgray=0;
	uint pixel_16;
	uchar pixel_24[3];
	uint pixel32;
	int y0, u, y1, v;
	
	for(in = 0; in < width * height * 2; in += 4) {
		pixel_16 =
		yuv[in + 3] << 24 |
		yuv[in + 2] << 16 |
		yuv[in + 1] <<  8 |
		yuv[in + 0];//YUV422每个像素2字节，每两个像素共用一个Cr,Cb值，即u和v，RGB24每个像素3个字节
		y0 = (pixel_16 & 0x000000ff);
		u  = (pixel_16 & 0x0000ff00) >>  8;
		y1 = (pixel_16 & 0x00ff0000) >> 16;
		v  = (pixel_16 & 0xff000000) >> 24;
		pixel32 = convert_yuv_to_rgb_pixel(y0, u, v);
		pixel_24[0] = (pixel32 & 0x000000ff);
		pixel_24[1] = (pixel32 & 0x0000ff00) >> 8;
		pixel_24[2] = (pixel32 & 0x00ff0000) >> 16;
		rgb[out++] = pixel_24[0];
		rgb[out++] = pixel_24[1];
		rgb[out++] = pixel_24[2];//rgb的一个像素
		pixel32 = convert_yuv_to_rgb_pixel(y1, u, v);
		pixel_24[0] = (pixel32 & 0x000000ff);
		pixel_24[1] = (pixel32 & 0x0000ff00) >> 8;
		pixel_24[2] = (pixel32 & 0x00ff0000) >> 16;
		rgb[out++] = pixel_24[0];
		rgb[out++] = pixel_24[1];
		rgb[out++] = pixel_24[2];
	}
	return 0;
}
//yuv 转换为灰度图像
int convert_yuv_to_gray(uchar *yuv, uchar *rgb, uint width,uint height)
{
	uint in, out = 0;
	int y0, y1;
	for(in = 0; in < width * height * 2; in += 4) {
		y0 = yuv[in + 0];
		y1 = yuv[in + 2];

		rgb[out++] = y0;
		rgb[out++] = y0;
		rgb[out++] = y0;//rgb的一个像素
		
		rgb[out++] = y1;
		rgb[out++] = y1;
		rgb[out++] = y1;
	}
	return 0;
}
//yuv 转换为灰度图像 单通道
int convert_yuv_to_gray0(uchar *yuv, uchar *gray, uint width,uint height)
{
	uint in, out = 0;
	int y0, y1;
	for(in = 0; in < width * height * 2; in += 4) {

		y0 = yuv[in + 0];
		y1 = yuv[in + 2];

		gray[out++] = y0;
		gray[out++] = y1;
	}
	return 0;
}

int motion_init( uint width, uint height )
{
	memset( &motion, 0, sizeof(motion));
	gBuf = (uchar*)calloc( 1, 3*width * height );
	if( gBuf == NULL)
	{
		printf("no enough mem ,malloc fail!\n");
		return -1;
	}

	motion.background = gBuf;
	motion.motion = gBuf + width * height;
	motion.binpic = gBuf + 2 * width * height;

	motion.flag = 0;
	motion.timecount = MAX_TIMECOUNT;
	motion.threshold = MAX_THRESHOLD;
	motion.noise = MAX_NOISE;
	return 0;
}
int motion_destroy( void )
{
	if( gBuf )	
	{
		free( gBuf );
		gBuf = NULL;
		motion.background = NULL;
		motion.binpic = NULL;
		motion.motion = NULL;
	}

	return 0;
}
/* Erodes a 3x3 box */
//3*3 侵蚀
static int erode9(unsigned char *img, int width, int height, void *buffer, unsigned char flag)
{    
	int y, i, sum = 0;    
	char *Row1,*Row2,*Row3;    
	Row1 = buffer;    
	Row2 = Row1 + width;    
	Row3 = Row1 + 2*width;    
	memset(Row2, flag, width);    
	memcpy(Row3, img, width);    
	for (y = 0; y < height; y++) {        
		memcpy(Row1, Row2, width);        
		memcpy(Row2, Row3, width);        
		if (y == height - 1)            
			memset(Row3, flag, width);        
		else            
			memcpy(Row3, img+(y + 1) * width, width);        
		for (i = width-2; i >= 1; i--) {            
			if (Row1[i-1] == 0 ||                
				Row1[i]   == 0 ||                
				Row1[i+1] == 0 ||                
				Row2[i-1] == 0 ||                
				Row2[i]   == 0 ||                
				Row2[i+1] == 0 ||                
				Row3[i-1] == 0 ||                
				Row3[i]   == 0 ||                
				Row3[i+1] == 0)                
				img[y * width + i] = 0;            
			else                
				sum++;        
		}        
		img[y * width] = img[y * width + width - 1] = flag;    
	}    
	return sum;
}

//3*3 膨胀
/* Dilates a 3x3 box */
static int dilate9(unsigned char *img, int width, int height, void *buffer)
{    
	/* - row1, row2 and row3 represent lines in the temporary buffer      
	 * - window is a sliding window containing max values of the columns     
	 *   in the 3x3 matrix     
	 * - widx is an index into the sliding window (this is faster than      
	 *   doing modulo 3 on i)     
	 * - blob keeps the current max value     
	 */    
	int y, i, sum = 0, widx;    
	unsigned char *row1, *row2, *row3, *rowTemp,*yp;    
	unsigned char window[3], blob, latest;    
	/* Set up row pointers in the temporary buffer. */    
	row1 = buffer;    
	row2 = row1 + width;    
	row3 = row2 + width;    
	/* Init rows 2 and 3. */    
	memset(row2, 0, width);    
	memcpy(row3, img, width);    
	/* Pointer to the current row in img. */    
	yp = img;        
	for (y = 0; y < height; y++) {        
		/* Move down one step; row 1 becomes the previous row 2 and so on. */
		rowTemp = row1;        
		row1 = row2;        
		row2 = row3;        
		row3 = rowTemp;        
		/* If we're at the last row, fill with zeros, otherwise copy from img. */        
		if (y == height - 1)            
			memset(row3, 0, width);        
		else            
			memcpy(row3, yp + width, width);                
		/* Init slots 0 and 1 in the moving window. */        
		window[0] = MAX3(row1[0], row2[0], row3[0]);        
		window[1] = MAX3(row1[1], row2[1], row3[1]);        
		/* Init blob to the current max, and set window index. */        
		blob = MAX2(window[0], window[1]);        
		widx = 2;        
		/* Iterate over the current row; index i is off by one to eliminate 
		 * a lot of +1es in the loop.         
		 */        
		for (i = 2; i <= width - 1; i++) {            
			/* Get the max value of the next column in the 3x3 matrix. */   
			latest = window[widx] = MAX3(row1[i], row2[i], row3[i]);    
			/* If the value is larger than the current max, use it.	Otherwise,             
			* calculate a new max (because the new value may not be the max.          
			*/            
			if (latest >= blob)                
				blob = latest;            
			else                
				blob = MAX3(window[0], window[1], window[2]);            
			/* Write the max value (blob) to the image. */            
			if (blob != 0) {                
				*(yp + i - 1) = blob;                
				sum++;            
			}            
			/* Wrap around the window index if necessary. */            
			if (++widx == 3)                
				widx = 0;        
		}        
		/* Store zeros in the vertical sides. */        
		*yp = *(yp + width - 1) = 0;        
		yp += width;    
	}        
	return sum;
}

//标识变化区域，同时该区域还是一个计数器，每次更新都要将标记位置减1 当标志位置减为0时，说明该位置已经不在运动
//由于拍照时间很快，如果单色物体在摄像区域内做平移运动时，如果不使用计数器方式，会造成检测到的运动部位只存在与交界区域。
//为了克服上面的问题，使用延迟计数方式，保证能标识出完整的变化部分
int pic_mark( unsigned char *motion, unsigned char *mark, unsigned int size )
{
    int i=0;
	int cnt = 0;
    
	while( i != size ) {
	    if(motion[i] > 0) motion[i]--;
	    if( mark ) {
		    if( mark[i] ) {
			    cnt++;
			    motion[i] = BRIGHTNESS;
		    }
		}
		i++;
	}

	return cnt;
}
//标识出变化区域，目前是通过改变变化区域的亮度进行标识
void pic_add( unsigned char *rgbimg, unsigned char *mark, unsigned int size )
{
    int i=0;
    int tmp;
    
	while( i != size ) {
		if( mark[i] ) {
		    tmp = rgbimg[i*3] + mark[i];
	        rgbimg[i*3] = tmp>255?255:tmp;
		}
		i++;
	}
}
  
//图像减法运算，返回变化的像素个数,结果存储在img1中
int pic_subtraction( unsigned char *img1, unsigned char *img2, unsigned int size )
{
	int i=0;
	//int cnt = 0;

	while( i != size ) {
		//像素值不能出现反转，求绝对值
		img2[i] = img1[i] > img2[i]?img1[i] - img2[i]:img2[i] - img1[i];
		i++;
	}

	return 0;
}
//图像二值化处理，小于noise 像素置0 大于noise 像素置 255，返回变化像素个数
int pic_binmap( unsigned char *img, unsigned char *binimg, unsigned char noise, unsigned int size )
{
	int i=0;
	int cnt = 0;

	while( i != size ) {
		binimg[i] = img[i] > noise?255:0;
		if( binimg[i] )
			cnt++;
		i++;
	}

	return cnt;
}
//图像二值化处理，小于noise 像素置0 大于noise 像素置 255，返回变化像素个数
int pic_noise_filter( unsigned char *img, unsigned char noise, unsigned int size )
{
	int i=0;
	int cnt = 0;

	while( i != size ) {
		if( img[i] < noise ) {
			img[i] = 0;
		} else {
			cnt++;
		}
		i++;
	}

	return cnt;
}
int noise_tune( unsigned char *img, unsigned int width, unsigned int height )
{
	long sum = 0;
	int cnt = 0;
	int i;
	unsigned char pmax,pmin;

	pmax = pmin = 0;
	for(i=0;i!=width*height;i++)
	{
		if( img[i] ) {
			cnt++;
			sum += img[i];
			pmax = img[i] > pmax?img[i]:pmax;
		}		
	}

	int avg = sum / cnt;

	printf("pmax = %d cnt = %d avg=%d\n",pmax,cnt,avg);
	printf("+++++++++++++++++++++++++\n");

	return avg;
}
void param_init( int noise, int threshold )
{
	if( noise > 0 )
		MAX_NOISE = noise;
	if( threshold > 0 )
		MAX_THRESHOLD = threshold;
}
int motion_check( unsigned char *yuv, unsigned char *out, unsigned int width, unsigned int height )
{
	int change = 0;
	
	if( 0 > flag_init ) {
		if( !motion_init( width , height ) ) {
			flag_init = 0;//初始化完成
			motion.width = width;
			motion.height = height;
			printf("motion.background saved\n");
			//保存背景图片
			convert_yuv_to_gray0( yuv, motion.background, width, height );
			return 0;
		} else {
			printf("motion init fail!\n");
			return -1;
		}
	}
	//先转换为单通道灰度图
	convert_yuv_to_gray0( yuv, motion.motion, width , height );
	pic_subtraction( motion.motion, motion.background, width * height );

	uchar *tmp = motion.background;
	motion.background = motion.motion;//更新背景
	motion.motion = tmp;
	
	//运动区域更新
	//pic_mark( motion.motion, NULL, width * height );
	convert_yuv_to_rgb_buffer( yuv, out,  width, height);
	//noise_tune( motion.motion, width, height);

	printf("noise=%d\n",motion.noise);
	change = pic_noise_filter( motion.motion, motion.noise, width * height );
	
	printf("change=%d\n",change);
	printf("threshold=%d\n",motion.threshold);
	printf("-------------------------------\n");
	if( change > motion.threshold ) {//如果去除噪声后改变量依然大于运动检测阈值则进行下一步运算
		unsigned char *tmpbuf;
		tmpbuf = (unsigned char *)calloc(1,width*3);
		//腐蚀处理
		printf("erode9=%d\n",erode9( motion.motion, width, height, tmpbuf, 0 ));
		//膨胀处理
		printf("dilate9=%d\n",dilate9( motion.motion, width, height, tmpbuf ));
		//标识出变化区域
		pic_add( out, motion.motion, width * height );
		free(tmpbuf);

		return 0xaa;//检测到运动
	}

	
	return 0;
}

