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

	
	// Ion
	IonBuffer videoBuffer(fb2.Length());
	
	void* videoBufferPtr = videoBuffer.Map();
	memset(videoBufferPtr, 0, videoBuffer.BufferSize());


	// Clear the LCD display
	uint16_t* fb2mem = (uint16_t*)fb2.Data();

	for (int y = 0; y < fb2.Height(); ++y)
	{
		for (int x = 0; x < fb2.Width(); ++x)
		{
			size_t offset = y * fb2.Width() + x;

			// 16 bit color
			//fb2mem[offset] = 0xf800;	// Red
			//fb2mem[offset] = 0x07e0;	// Green
			//fb2mem[offset] = 0x001f;	// Blue
			//fb2mem[offset] = 0xffff;	// White
			fb2mem[offset] = 0x0000;	// Black
		}
	}


	// Configure GE2D
	struct config_para_ex_s configex = { 0 };

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

	//configex.src_para.mem_type = CANVAS_OSD0;
	//configex.src_para.left = 0;
	//configex.src_para.top = 0;
	//configex.src_para.width = fb0.Width();
	//configex.src_para.height = fb0.Height();

	int src_index = 0x8180; //0x01c3; // ((0xc3 & 0xff) | (((0x00) << 8) & 0x0000ff00));
	configex.src_para.mem_type = CANVAS_TYPE_INVALID; //CANVAS_ALLOC;
	configex.src_para.format = GE2D_FORMAT_S8_Y; // GE2D_FORMAT_S8_Y; //GE2D_FORMAT_M24_NV21;
	configex.src_para.canvas_index = src_index;
	//configex.src_para.left = 0;
	//configex.src_para.top = 0;
	//configex.src_para.width = 1920;
	//configex.src_para.height = 1088;
	//0x77ce5000-0x64800000-0x64800000
	//3840-800-800
	//544-608-608
	//configex.src_planes[0].addr = (long unsigned int)0x770f1000;
	//configex.src_planes[0].w = 3840;
	//configex.src_planes[0].h = 544;
	//configex.src_planes[1].addr = (long unsigned int)0x64800000;
	//configex.src_planes[1].w = 800;
	//configex.src_planes[1].h = 608;
	/*configex.src_planes[3].addr = (long unsigned int)0x64800000;
	configex.src_planes[3].w = 800;
	configex.src_planes[3].h = 608;*/

	configex.src2_para.mem_type = CANVAS_TYPE_INVALID;

	configex.dst_para.mem_type = CANVAS_ALLOC;
	configex.dst_para.format = GE2D_FORMAT_S16_RGB_565;
	configex.dst_para.left = 0;
	configex.dst_para.top = 0;
	configex.dst_para.width = fb2.Width();
	configex.dst_para.height = fb2.Height();
	configex.dst_planes[0].addr = (long unsigned int)fb2.PhysicalAddress();
	configex.dst_planes[0].w = fb2.Width();
	configex.dst_planes[0].h = fb2.Height();

	io = ioctl(ge2d_fd, GE2D_CONFIG_EX, &configex);
	if (io < 0)
	{
		throw Exception("GE2D_CONFIG_EX failed.\n");
	}


	// Aspect ratio
	const float LCD_ASPECT = 1.5f;	// LCD aspect = 3:2 = 1.5

	// If no aspect ratio was specified, calculate it
	if (aspect == -1)
	{
		aspect = (float)fb0.Width() / (float)fb0.Height();
	}

	int dstX;
	int dstY;
	int dstWidth;
	int dstHeight;

	if (aspect == LCD_ASPECT)
	{
		dstWidth = fb2.Width();
		dstHeight = fb2.Height();
		dstX = 0;
		dstY = 0;
	}
	else if (aspect < LCD_ASPECT)
	{
		dstWidth = fb2.Height() * aspect;
		dstHeight = fb2.Height();
		dstX = (fb2.Width() / 2) - (dstWidth / 2);
		dstY = 0;
	}
	else
	{
		dstWidth = fb2.Width();
		dstHeight = fb2.Width() * (1.0f / aspect);
		dstX = 0;
		dstY = (fb2.Height() / 2) - (dstHeight / 2);
	}

	printf("aspect=%f\n", aspect);


	//  Blit rectangle
	ge2d_para_s blitRect = { 0 };

	blitRect.src1_rect.x = 0;
	blitRect.src1_rect.y = 0;
	blitRect.src1_rect.w = configex.src_para.width;
	blitRect.src1_rect.h = configex.src_para.height;

	blitRect.dst_rect.x = dstX;
	blitRect.dst_rect.y = dstY;
	blitRect.dst_rect.w = dstWidth;
	blitRect.dst_rect.h = dstHeight;

	
	bool skipFlag = false;

	while (true)
	{
		if (skipFlag)
		{
			fb0.WaitForVSync();
		}
		else
		{
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


					//configex.src_para.mem_type = CANVAS_OSD0;
					//configex.src_para.canvas_index = 0;
					//configex.src_para.left = 0;
					//configex.src_para.top = 0;
					//configex.src_para.width = fb0.Width();
					//configex.src_para.height = fb0.Height();

					//switch (fb0.BitsPerPixel())
					//{
					//	case 16:
					//		configex.src_para.format = GE2D_FORMAT_S16_RGB_565;
					//		break;

					//	case 24:
					//		configex.src_para.format = GE2D_FORMAT_S24_RGB;
					//		break;

					//	case 32:
					//		configex.src_para.format = GE2D_FORMAT_S32_ARGB;
					//		break;

					//	default:
					//		throw Exception("fb0 bits per pixel not supported");
					//}


					configex.src_para.mem_type = CANVAS_TYPE_INVALID;
					configex.src_para.canvas_index = canvas0addr; //((canvas_index << 8) | canvas0addr);
					configex.src_para.left = 0;
					configex.src_para.top = 0;
					configex.src_para.width = videoWidth;
					configex.src_para.height = videoHeight / 2;
					configex.src_para.format = ge2dformat;

					configex.dst_para.mem_type = CANVAS_ALLOC;
					configex.dst_para.format = GE2D_FORMAT_S16_RGB_565;
					configex.dst_para.left = 0;
					configex.dst_para.top = 0;
					configex.dst_para.width = fb2.Width();
					configex.dst_para.height = fb2.Height();
					configex.dst_planes[0].addr = (long unsigned int)videoBuffer.PhysicalAddress();
					configex.dst_planes[0].w = fb2.Width();
					configex.dst_planes[0].h = fb2.Height();


					io = ioctl(ge2d_fd, GE2D_CONFIG_EX, &configex);
					if (io < 0)
					{
						throw Exception("video GE2D_CONFIG_EX failed.\n");
					}


					//blitRect.src1_rect.x = 0;
					//blitRect.src1_rect.y = 0;
					//blitRect.src1_rect.w = configex.src_para.width;
					//blitRect.src1_rect.h = configex.src_para.height;


					blitRect.src1_rect.x = 0;
					blitRect.src1_rect.y = 0;
					blitRect.src1_rect.w = configex.src_para.width;
					blitRect.src1_rect.h = configex.src_para.height;


					//CorrectAspect((float)videoWidth / (float)videoHeight, &dstX, &dstY, &dstWidth, &dstHeight);
					CorrectAspect(aspect, &dstX, &dstY, &dstWidth, &dstHeight);

					blitRect.dst_rect.x = dstX;
					blitRect.dst_rect.y = dstY;
					blitRect.dst_rect.w = dstWidth;
					blitRect.dst_rect.h = dstHeight;


					//blitRect.op = (OPERATION_ADD << 24) |
					//	(COLOR_FACTOR_SRC_ALPHA << 20) |
					//	(COLOR_FACTOR_ONE_MINUS_SRC_ALPHA << 16) |
					//	(OPERATION_ADD << 8) |
					//	(COLOR_FACTOR_ONE << 4) |
					//	(COLOR_FACTOR_ZERO << 0);

					// Color conversion
					io = ioctl(ge2d_fd, GE2D_STRETCHBLIT_NOALPHA, &blitRect); //GE2D_STRETCHBLIT_NOALPHA
					if (io < 0)
					{
						throw Exception("GE2D_STRETCHBLIT_NOALPHA failed.");
					}


					io = ioctl(amvideo_fd, AMVIDEO_EXT_PUT_CURRENT_VIDEOFRAME);
					if (io < 0)
					{
						throw Exception("AMSTREAM_EXT_PUT_CURRENT_VIDEOFRAME failed.");
					}
				}
#endif
			}


#if 1
			// OSD blit

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
			configex.src_para.y_rev = 0;

			configex.src2_para.mem_type = CANVAS_ALLOC;
			configex.src2_para.format = GE2D_FORMAT_S16_RGB_565;
			configex.src2_para.left = 0;
			configex.src2_para.top = 0;
			configex.src2_para.width = fb2.Width();
			configex.src2_para.height = fb2.Height();
			configex.src2_planes[0].addr = (long unsigned int)videoBuffer.PhysicalAddress();
			configex.src2_planes[0].w = fb2.Width();
			configex.src2_planes[0].h = fb2.Height();
			configex.src2_para.y_rev = 0;

			configex.dst_para.mem_type = CANVAS_ALLOC;
			configex.dst_para.format = GE2D_FORMAT_S16_RGB_565;
			configex.dst_para.left = 0;
			configex.dst_para.top = 0;
			configex.dst_para.width = fb2.Width();
			configex.dst_para.height = fb2.Height();
			configex.dst_planes[0].addr = (long unsigned int)fb2.PhysicalAddress();
			configex.dst_planes[0].w = fb2.Width();
			configex.dst_planes[0].h = fb2.Height();


			io = ioctl(ge2d_fd, GE2D_CONFIG_EX, &configex);
			if (io < 0)
			{
				throw Exception("osd GE2D_CONFIG_EX failed.\n");
			}


			blitRect.src1_rect.x = 0;
			blitRect.src1_rect.y = 0;
			blitRect.src1_rect.w = configex.src_para.width;
			blitRect.src1_rect.h = configex.src_para.height;

			blitRect.src2_rect.x = 0;
			blitRect.src2_rect.y = 0;
			blitRect.src2_rect.w = configex.src2_para.width;
			blitRect.src2_rect.h = configex.src2_para.height;


			CorrectAspect(aspect, &dstX, &dstY, &dstWidth, &dstHeight);

			blitRect.dst_rect.x = dstX;
			blitRect.dst_rect.y = dstY;
			blitRect.dst_rect.w = dstWidth;
			blitRect.dst_rect.h = dstHeight;


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
