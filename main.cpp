#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>

#include "ge2d.h"
#include "ge2d_cmd.h"
#include "ge2d_func.h"

#include "Exception.h"
#include "FrameBuffer.h"
#include "IonBuffer.h"

#define FBTFT_UPDATE             _IO('m', 313)

//const int DISPLAY_CANVAS_BASE_INDEX = 0x70; // 0x60;
//#define PPMGR_CANVAS_INDEX 0x70

#define AMVIDEO_MAGIC  'X'

#define AMVIDEO_EXT_GET_CURRENT_VIDEOFRAME _IOR((AMVIDEO_MAGIC), 0x01, int32_t)
#define AMVIDEO_EXT_PUT_CURRENT_VIDEOFRAME _IO((AMVIDEO_MAGIC), 0x02)

#define AMVIDEO_EXT_CURRENT_VIDEOFRAME_GET_GE2D_FORMAT _IOR((AMVIDEO_MAGIC), 0x03, uint32_t)
#define AMVIDEO_EXT_CURRENT_VIDEOFRAME_GET_SIZE _IOR((AMVIDEO_MAGIC), 0x04, uint64_t)
#define AMVIDEO_EXT_CURRENT_VIDEOFRAME_GET_CANVAS0ADDR _IOR((AMVIDEO_MAGIC), 0x05, uint32_t)


struct option longopts[] = {
	{ "aspect",			required_argument,  NULL,          'a' },
	{ 0, 0, 0, 0 }
};


void ShowUsage()
{
	printf("Usage: c2screen2lcd [OPTIONS]\n");
	printf("Displays main framebuffer on LCD shield.\n\n");

	printf("  -a, --aspect h:w\tForce aspect ratio\n");
}

struct Rectangle
{
	int X;
	int Y;
	int Width;
	int Height;
};

struct Size
{
	int Width;
	int Height;

	Size(int width, int height)
		: Width(width), Height(height)
	{
	}
};

void CalculateRectangle(Size targetSize, float targetAspect, float sourceAspect,  Rectangle* outRectangle)
{
	//printf("CalculateRectangle: targetSize=%d,%d targetAspect=%f sourceAspect=%f\n", targetSize.Width, targetSize.Height, targetAspect, sourceAspect);

	// Aspect ratio
	int dstX;
	int dstY;
	int dstWidth;
	int dstHeight;

/*	if (targetAspect == sourceAspect)
	{
		dstWidth = targetSize.Width;
		dstHeight = targetSize.Height;
		dstX = 0;
		dstY = 0;
	}
	else*/ 
	if (sourceAspect >= targetAspect)
	{
		dstWidth = targetSize.Width;
		dstHeight = targetSize.Width * (1.0f / sourceAspect);
		dstX = 0;
		dstY = (targetSize.Height / 2.0f) - (dstHeight / 2.0f);

		//printf("CalculateRectangle: %d, %d - %d x %d\n", dstX, dstY, dstWidth, dstHeight);
	}
	else
	{
		dstWidth = targetSize.Height * sourceAspect;
		dstHeight = targetSize.Height;
		dstX = (targetSize.Width / 2.0f) - (dstWidth / 2.0f);
		dstY = 0;
	}

	(*outRectangle).X = dstX;
	(*outRectangle).Y = dstY;
	(*outRectangle).Width = dstWidth;
	(*outRectangle).Height = dstHeight;
}

void CorrectAspect(float aspect, int* x, int* y, int* width, int* height)
{
	// Aspect ratio
	const float LCD_ASPECT = 1.5f;	// LCD aspect = 3:2 = 1.5
	const int LCD_WIDTH = 480;
	const int LCD_HEIGHT = 320;

	int dstX;
	int dstY;
	int dstWidth;
	int dstHeight;

	if (aspect == LCD_ASPECT)
	{
		dstWidth = LCD_WIDTH;
		dstHeight = LCD_HEIGHT;
		dstX = 0;
		dstY = 0;
	}
	else if (aspect < LCD_ASPECT)
	{
		dstWidth = LCD_HEIGHT * aspect;
		dstHeight = LCD_HEIGHT;
		dstX = (LCD_WIDTH / 2) - (dstWidth / 2);
		dstY = 0;
	}
	else
	{
		dstWidth = LCD_WIDTH;
		dstHeight = LCD_WIDTH * (1.0f / aspect);
		dstX = 0;
		dstY = (LCD_HEIGHT / 2) - (dstHeight / 2);
	}

	*x = dstX;
	*y = dstY;
	*width = dstWidth;
	*height = dstHeight;
}


int main(int argc, char** argv)
{
	int io;


	// options
	int c;
	float aspect = -1;

	while ((c = getopt_long(argc, argv, "a:", longopts, NULL)) != -1)
	{
		switch (c)
		{
			case 'a':
			{
				if (strchr(optarg, ':'))
				{
					unsigned int h;
					unsigned int w;
					double s;
					if (sscanf(optarg, "%u:%u", &h, &w) == 2)
					{
						aspect = (float)h / (float)w;
					}
					else
					{
						throw Exception("invalid aspect");
					}
				}
				else
				{
					aspect = atof(optarg);
				}
			}
			break;

			default:
				ShowUsage();
				exit(EXIT_FAILURE);
		}
	}


	// HDMI (ARGB32)
	FrameBuffer fb0("/dev/fb0");
	printf("fb0: screen info - width=%d, height=%d, bpp=%d addr=%p\n", fb0.Width(), fb0.Height(), fb0.BitsPerPixel(), fb0.PhysicalAddress());


	// LCD (RGB565)
	FrameBuffer fb2("/dev/fb2");
	printf("fb2: screen info - width=%d, height=%d, bpp=%d addr=%p\n", fb2.Width(), fb2.Height(), fb2.BitsPerPixel(), fb2.PhysicalAddress());

	if (fb2.BitsPerPixel() != 16)
	{
		throw Exception("Unexpected fb2 bits per pixel");
	}


	// GE2D
	int ge2d_fd = open("/dev/ge2d", O_RDWR);
	if (ge2d_fd < 0)
	{
		throw Exception("open /dev/ge2d failed.");
	}


	// Amvideo
	int amvideo_fd = open("/dev/amvideo", O_RDWR);
	if (amvideo_fd < 0)
	{
		throw Exception("open /dev/amvideo failed.");
	}

	
	// Clear the LCD display
	memset(fb2.Data(), 0, fb2.Length());


	// Aspect ratio
	const float LCD_ASPECT = 1.5f;	// LCD aspect = 3:2 = 1.5

	// If no aspect ratio was specified, calculate it
	if (aspect == -1)
	{
		aspect = (float)fb0.Width() / (float)fb0.Height();
	}

	printf("aspect=%f\n", aspect);


	Rectangle frameBufferRect;
	CalculateRectangle(Size(480, 320), LCD_ASPECT, aspect, &frameBufferRect);


	// Ion
	IonBuffer videoBuffer(fb0.Width() * fb0.Height() * 2); // RGB565

	void* videoBufferPtr = videoBuffer.Map();
	memset(videoBufferPtr, 0, videoBuffer.BufferSize());



	struct config_para_ex_s configex = { 0 };
	ge2d_para_s blitRect = { 0 };
	bool skipFlag = false;
	int frameCount = 0;

	while (true)
	{
		++frameCount;

		if (frameCount % 5 != 0)
		{
			fb0.WaitForVSync();
		}
		else
		{
			//printf("frameBufferRect=%d,%d %dx%d\n", frameBufferRect.X, frameBufferRect.Y, frameBufferRect.Width, frameBufferRect.Height);

			if (!fb0.GetTransparencyEnabled())
			{
#if 1
				// Video
				int canvas_index;
				io = ioctl(amvideo_fd, AMVIDEO_EXT_GET_CURRENT_VIDEOFRAME, &canvas_index);
				if (io < 0)
				{
					//throw Exception("AMSTREAM_EXT_GET_CURRENT_VIDEOFRAME failed.");
				}
				else
				{
					//exit(1);

					//printf("amvideo: canvas_index=%x\n", canvas_index);

					uint32_t canvas0addr;
					io = ioctl(amvideo_fd, AMVIDEO_EXT_CURRENT_VIDEOFRAME_GET_CANVAS0ADDR, &canvas0addr);
					if (io < 0)
					{
						throw Exception("AMSTREAM_EXT_CURRENT_VIDEOFRAME_GET_CANVAS0ADDR failed.");
					}

					//printf("amvideo: canvas0addr=%x\n", canvas0addr);


					uint32_t ge2dformat;
					io = ioctl(amvideo_fd, AMVIDEO_EXT_CURRENT_VIDEOFRAME_GET_GE2D_FORMAT, &ge2dformat);
					if (io < 0)
					{
						throw Exception("AMSTREAM_EXT_CURRENT_VIDEOFRAME_GET_GE2D_FORMAT failed.");
					}

					//printf("amvideo: ge2dformat=%x\n", ge2dformat);


					uint64_t size;
					io = ioctl(amvideo_fd, AMVIDEO_EXT_CURRENT_VIDEOFRAME_GET_SIZE, &size);
					if (io < 0)
					{
						throw Exception("AMSTREAM_EXT_CURRENT_VIDEOFRAME_GET_SIZE failed.");
					}

					int videoWidth = size >> 32;
					int videoHeight = size & 0xffffff;
					//printf("amvideo: size=%x (%dx%d)\n", size, size >> 32, size & 0xffffff);

					Rectangle videoRect;
					float videoAspect = (float)videoWidth / (float)videoHeight;
					//CalculateRectangle(Size(fb2.Width(), fb2.Height()), aspect, videoAspect, &videoRect);


					//Rectangle dstRect;

					
					// convert to display aspect
					CalculateRectangle(Size(fb0.Width(), fb0.Height()), aspect, videoAspect, &videoRect);
					//printf("videoRect=%d,%d %dx%d\n", videoRect.X, videoRect.Y, videoRect.Width, videoRect.Height);


					//// convert to LCD aspect
					//CalculateRectangle(Size(fb2.Width(), fb2.Height()), LCD_ASPECT, aspect, &dstRect);
					//videoRect = dstRect;

					// ---
					configex = { 0 };

#if 0
					// Clear
					

					configex.dst_para.mem_type = CANVAS_ALLOC;
					configex.dst_para.format = GE2D_FORMAT_S16_RGB_565;
					configex.dst_para.left = 0;
					configex.dst_para.top = 0;

					configex.dst_para.width = fb2.Width();
					configex.dst_para.height = fb2.Height();
					configex.dst_planes[0].addr = (long unsigned int)videoBuffer.PhysicalAddress();
					configex.dst_planes[0].w = configex.dst_para.width;
					configex.dst_planes[0].h = configex.dst_para.height;

					io = ioctl(ge2d_fd, GE2D_CONFIG_EX, &configex);
					if (io < 0)
					{
						throw Exception("video GE2D_CONFIG_EX failed.\n");
					}


					blitRect.dst_rect.x =0;
					blitRect.dst_rect.y = 0;
					blitRect.dst_rect.w = fb2.Width();
					blitRect.dst_rect.h = fb2.Height();
					blitRect.color = 0x000000ff;	// RGBA

					io = ioctl(ge2d_fd, GE2D_FILLRECTANGLE, &blitRect);
					if (io < 0)
					{
						throw Exception("GE2D_STRETCHBLIT_NOALPHA failed.");
					}
#endif

					// ---

					configex.src_para.mem_type = CANVAS_TYPE_INVALID;
					configex.src_para.canvas_index = canvas0addr;
					configex.src_para.left = 0;
					configex.src_para.top = 0;
					configex.src_para.width = videoWidth;
					configex.src_para.height = videoHeight / 2;
					configex.src_para.format = ge2dformat;
					

					configex.dst_para.mem_type = CANVAS_ALLOC;
					configex.dst_para.format = GE2D_FORMAT_S16_RGB_565;
					configex.dst_para.left = 0;
					configex.dst_para.top = 0;
#if 1
					configex.dst_para.width = fb0.Width();
					configex.dst_para.height = fb0.Height();
					configex.dst_planes[0].addr = (long unsigned int)videoBuffer.PhysicalAddress();
					configex.dst_planes[0].w = configex.dst_para.width;
					configex.dst_planes[0].h = configex.dst_para.height;
#else
					configex.dst_para.width = fb2.Width();
					configex.dst_para.height = fb2.Height();
					configex.dst_planes[0].addr = (long unsigned int)fb2.PhysicalAddress();
					configex.dst_planes[0].w = configex.dst_para.width;
					configex.dst_planes[0].h = configex.dst_para.height;
#endif
					io = ioctl(ge2d_fd, GE2D_CONFIG_EX, &configex);
					if (io < 0)
					{
						throw Exception("video GE2D_CONFIG_EX failed.\n");
					}


					//CorrectAspect((float)videoWidth / (float)videoHeight, &dstX, &dstY, &dstWidth, &dstHeight);
					//CorrectAspect(aspect, &dstX, &dstY, &dstWidth, &dstHeight);
					


					blitRect.src1_rect.x = 0;
					blitRect.src1_rect.y = 0;
					blitRect.src1_rect.w = configex.src_para.width;
					blitRect.src1_rect.h = configex.src_para.height;

					blitRect.dst_rect.x = videoRect.X;
					blitRect.dst_rect.y = videoRect.Y;
					blitRect.dst_rect.w = videoRect.Width;
					blitRect.dst_rect.h = videoRect.Height;

					//printf("rect=%d,%d %dx%d\n", videoRect.X, videoRect.Y, videoRect.Width, videoRect.Height);




					// Blit to videoBuffer
					io = ioctl(ge2d_fd, GE2D_STRETCHBLIT_NOALPHA, &blitRect);
					if (io < 0)
					{
						throw Exception("GE2D_STRETCHBLIT_NOALPHA failed.");
					}


					// Return video frame
					io = ioctl(amvideo_fd, AMVIDEO_EXT_PUT_CURRENT_VIDEOFRAME);
					if (io < 0)
					{
						throw Exception("AMSTREAM_EXT_PUT_CURRENT_VIDEOFRAME failed.");
					}

					
					// Blit to LCD
					configex.src_para.mem_type = CANVAS_ALLOC;
					configex.src_para.format = GE2D_FORMAT_S16_RGB_565;
					configex.src_para.canvas_index = 0;
					configex.src_para.left = 0;
					configex.src_para.top = 0;
					configex.src_para.width = fb0.Width();
					configex.src_para.height = fb0.Height();
					configex.src_planes[0].addr = (long unsigned int)videoBuffer.PhysicalAddress();
					configex.src_planes[0].w = configex.src_para.width;
					configex.src_planes[0].h = configex.src_para.height;

					configex.dst_para.mem_type = CANVAS_ALLOC;
					configex.dst_para.format = GE2D_FORMAT_S16_RGB_565;
					configex.dst_para.left = 0;
					configex.dst_para.top = 0;
					configex.dst_para.width = fb2.Width();
					configex.dst_para.height = fb2.Height();
					configex.dst_planes[0].addr = (long unsigned int)fb2.PhysicalAddress();
					configex.dst_planes[0].w = configex.dst_para.width;
					configex.dst_planes[0].h = configex.dst_para.height;

					io = ioctl(ge2d_fd, GE2D_CONFIG_EX, &configex);
					if (io < 0)
					{
						throw Exception("video GE2D_CONFIG_EX failed.\n");
					}


					blitRect.src1_rect.x = 0;
					blitRect.src1_rect.y = 0;
					blitRect.src1_rect.w = configex.src_para.width;
					blitRect.src1_rect.h = configex.src_para.height;

					blitRect.dst_rect.x = frameBufferRect.X; // 0;
					blitRect.dst_rect.y = frameBufferRect.Y; //0;
					blitRect.dst_rect.w = frameBufferRect.Width; //configex.dst_para.width;
					blitRect.dst_rect.h = frameBufferRect.Height; //configex.dst_para.height;

					io = ioctl(ge2d_fd, GE2D_STRETCHBLIT_NOALPHA, &blitRect);
					if (io < 0)
					{
						throw Exception("GE2D_STRETCHBLIT_NOALPHA failed.");
					}
				}
#endif
			}


#if 1
			// OSD blit
			configex = { 0 };

			switch (fb0.BitsPerPixel())
			{
			case 16:
				configex.src_para.format = GE2D_FORMAT_S16_RGB_565;
				break;

			case 24:
				configex.src_para.format = GE2D_FORMAT_S24_RGB;
				break;

			case 32:
				configex.src_para.format = GE2D_FORMAT_S32_ARGB;
				break;

			default:
				throw Exception("fb0 bits per pixel not supported");
			}

			configex.src_para.mem_type = CANVAS_OSD0;
			configex.src_para.canvas_index = 0;
			configex.src_para.left = 0;
			configex.src_para.top = 0;
			configex.src_para.width = fb0.Width();
			configex.src_para.height = fb0.Height();

			configex.src2_para.format = GE2D_FORMAT_S16_RGB_565;
			configex.src2_para.mem_type = CANVAS_ALLOC;
			configex.src2_para.left = 0;
			configex.src2_para.top = 0;
			configex.src2_para.width = fb2.Width();
			configex.src2_para.height = fb2.Height();
			configex.src2_planes[0].addr = (long unsigned int)fb2.PhysicalAddress();; //videoBuffer.PhysicalAddress();
			configex.src2_planes[0].w = configex.src2_para.width;
			configex.src2_planes[0].h = configex.src2_para.height;

			configex.dst_para.format = GE2D_FORMAT_S16_RGB_565;
			configex.dst_para.mem_type = CANVAS_ALLOC;
			configex.dst_para.left = 0;
			configex.dst_para.top = 0;
			configex.dst_para.width = fb2.Width();
			configex.dst_para.height = fb2.Height();
			configex.dst_planes[0].addr = (long unsigned int)fb2.PhysicalAddress();
			configex.dst_planes[0].w = configex.dst_para.width;
			configex.dst_planes[0].h = configex.dst_para.height;

			io = ioctl(ge2d_fd, GE2D_CONFIG_EX, &configex);
			if (io < 0)
			{
				throw Exception("osd GE2D_CONFIG_EX failed.\n");
			}


			blitRect.src1_rect.x = 0;
			blitRect.src1_rect.y = 0;
			blitRect.src1_rect.w = configex.src_para.width;
			blitRect.src1_rect.h = configex.src_para.height;

			blitRect.src2_rect.x = 	frameBufferRect.X; // 0
			blitRect.src2_rect.y = 	frameBufferRect.Y; // 0
			blitRect.src2_rect.w = frameBufferRect.Width; //configex.src2_para.width; //
			blitRect.src2_rect.h = frameBufferRect.Height; //configex.src2_para.height; //

			blitRect.dst_rect.x = frameBufferRect.X;	// 0;
			blitRect.dst_rect.y = frameBufferRect.Y;	//0;
			blitRect.dst_rect.w = frameBufferRect.Width;	//fb2.Width();
			blitRect.dst_rect.h = frameBufferRect.Height; //fb2.Height();

			//printf("rect=%d,%d %dx%d\n", frameBufferRect.X, frameBufferRect.Y, frameBufferRect.Width, frameBufferRect.Height);

			/*
			ge2d_cmd_cfg->color_blend_mode = (op >> 24) & 0xff;
			ge2d_cmd_cfg->color_src_blend_factor = (op >> 20) & 0xf;
			ge2d_cmd_cfg->color_dst_blend_factor = (op >> 16) & 0xf;
			ge2d_cmd_cfg->alpha_blend_mode = (op >> 8) & 0xff;
			ge2d_cmd_cfg->alpha_src_blend_factor = (op >>  4) & 0xf;
			ge2d_cmd_cfg->alpha_dst_blend_factor = (op >> 0) & 0xf;
			*/
			blitRect.op = (OPERATION_ADD << 24) |
				(COLOR_FACTOR_SRC_ALPHA << 20) |
				(COLOR_FACTOR_ONE_MINUS_SRC_COLOR << 16) |
				(OPERATION_ADD << 8) |
				(COLOR_FACTOR_SRC_ALPHA << 4) |
				(COLOR_FACTOR_ONE_MINUS_SRC_COLOR << 0);


			// Wait for VSync
			fb0.WaitForVSync();


			if (fb0.GetTransparencyEnabled())
			{
				io = ioctl(ge2d_fd, GE2D_STRETCHBLIT_NOALPHA, &blitRect);
			}
			else
			{
				io = ioctl(ge2d_fd, GE2D_BLEND, &blitRect);
			}

			if (io < 0)
			{
				throw Exception("GE2D_STRETCHBLIT_NOALPHA failed.");
			}
#endif

			// Update LCD
			//fb2.WaitForVSync();

			io = ioctl(fb2.FileDescriptor(), FBTFT_UPDATE);
			if (io < 0)
			{
				throw Exception("FBTFT_UPDATE failed.");
			}
		}

		skipFlag = !skipFlag;
	}


	// Terminate
	return 0;
}
