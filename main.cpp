#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>

#include "ion.h"
#include "meson_ion.h"
#include "ge2d.h"
#include "ge2d_cmd.h"

#include "IonBuffer.h"
#include "FrameBuffer.h"


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
	printf("fb0: screen info - width=%d, height=%d, bpp=%d\n", fb0.Width(), fb0.Height(), fb0.BitsPerPixel());


	// LCD (RGB565)
	FrameBuffer fb2("/dev/fb2");
	printf("fb2: screen info - width=%d, height=%d, bpp=%d\n", fb2.Width(), fb2.Height(), fb2.BitsPerPixel());

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


	// Ion
	IonBuffer lcdBuffer(fb2.Length());
	void* lcdBufferPtr = lcdBuffer.Map();


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
			fb2mem[offset] = 0xffff;	// White
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

	configex.src_para.mem_type = CANVAS_OSD0;
	configex.src_para.left = 0;
	configex.src_para.top = 0;
	configex.src_para.width = fb0.Width();
	configex.src_para.height = fb0.Height();

	configex.src2_para.mem_type = CANVAS_TYPE_INVALID;

	configex.dst_para.mem_type = CANVAS_ALLOC;
	configex.dst_para.format = GE2D_FORMAT_S16_RGB_565;
	configex.dst_para.left = 0;
	configex.dst_para.top = 0;
	configex.dst_para.width = fb2.Width();
	configex.dst_para.height = fb2.Height();
	configex.dst_planes[0].addr = lcdBuffer.PhysicalAddress();
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
	blitRect.src1_rect.w = fb0.Width();
	blitRect.src1_rect.h = fb0.Height();

	blitRect.dst_rect.x = dstX;
	blitRect.dst_rect.y = dstY;
	blitRect.dst_rect.w = dstWidth;
	blitRect.dst_rect.h = dstHeight;


	while (true)
	{
		// Wait for VSync
		fb0.WaitForVSync();

		// Color conversion
		io = ioctl(ge2d_fd, GE2D_STRETCHBLIT_NOALPHA, &blitRect);
		if (io < 0)
		{
			throw Exception("GE2D_STRETCHBLIT_NOALPHA failed.");
		}

		// Copy to LCD
		memcpy(fb2mem, lcdBufferPtr, lcdBuffer.BufferSize());
	}


	// Terminate
	return 0;
}
