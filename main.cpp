#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdint.h>

#include <linux/fb.h>

#include <exception>


class Exception : public std::exception
{
public:
	Exception(const char* message)
		: std::exception()
	{
		fprintf(stderr, "%s\n", message);
	}

};


int main()
{
	int io;


	// -- open fb0
	int fb0fd = open("/dev/fb0", O_RDWR);
	if (fb0fd < 0)
	{
		throw Exception("fb0: open failed.");
	}

	struct fb_var_screeninfo fb0info;
	io = ioctl(fb0fd, FBIOGET_VSCREENINFO, &fb0info);
	if (io < 0)
	{
		throw Exception("fb0: FBIOGET_VSCREENINFO failed.");
	}


	int fb0width = fb0info.xres;
	int fb0height = fb0info.yres;
	int fb0bpp = fb0info.bits_per_pixel;
	int fb0dataLen = fb0width * fb0height * (fb0bpp / 8);

	printf("fb0: screen info - width=%d, height=%d, bpp=%d\n", fb0width, fb0height, fb0bpp);

	// -- open fb2
	int fb2fd = open("/dev/fb2", O_RDWR);
	if (fb2fd < 0)
	{
		throw Exception("fb2: open failed.");
	}

	struct fb_var_screeninfo fb2info;
	io = ioctl(fb2fd, FBIOGET_VSCREENINFO, &fb2info);
	if (io < 0)
	{
		throw Exception("fb2: FBIOGET_VSCREENINFO failed.");
	}


	int fb2width = fb2info.xres;
	int fb2height = fb2info.yres;
	int fb2bpp = fb2info.bits_per_pixel;
	int fb2dataLen = fb2width * fb2height * (fb2bpp / 8);

	printf("fb2: screen info - width=%d, height=%d, bpp=%d\n", fb2width, fb2height, fb2bpp);


	// -- mmap
	uint32_t* fb0mem = (uint32_t*)mmap(0, fb0dataLen, PROT_WRITE | PROT_READ, MAP_SHARED, fb0fd, 0);
	if (fb0mem == MAP_FAILED)
	{
		throw Exception("fb0: mmap failed");
	}

	printf("fb0: mmap=%p\n", fb0mem);


	uint16_t* fb2mem = (uint16_t*)mmap(0, fb2dataLen, PROT_WRITE | PROT_READ, MAP_SHARED, fb2fd, 0);
	if (fb2mem == MAP_FAILED)
	{
		throw Exception("fb2: mmap failed");
	}

	printf("fb2: mmap=%p\n", fb2mem);


	// -- test
	
	for (int y = 0; y < fb2height; ++y)
	{
		for (int x = 0; x < fb2width; ++x)
		{
			size_t offset = y * fb2width + x;
			uint16_t* ptr = fb2mem + offset;

			// 16 bit color
			//*ptr = 0xf800;	//Red
			*ptr = 0x07e0;		//Green
			//*ptr = 0x001f;		//Blue
		}
	}

	// LCD = RGB565
	// HDMI = ARGB32

#if 1
	while (true)
	{
		// -- copy
		for (int y = 0; y < fb2height; ++y)
		{
			for (int x = 0; x < fb2width; ++x)
			{
				size_t srcOffset = y * fb0width + x;
				uint32_t* srcPtr = fb0mem + srcOffset;

				int pixel = fb0mem[srcOffset];

				size_t offset = y * fb2width + x;
				uint16_t* ptr = fb2mem + offset;

				// 16 bit color
				uint8_t blue = (pixel & 0x000000ff) >> 0;
				uint8_t green = (pixel & 0x0000ff00) >> 8;
				uint8_t red = (pixel & 0x00ff0000) >> 16;

				red = (uint8_t)((float)red / 255.0f * 0x1f);
				green = (uint8_t)((float)green / 255.0f * 0x3f);
				blue = (uint8_t)((float)blue / 255.0f * 0x1f);

				*ptr = ((red & 0x1f) << 11) |
					((green & 0x3f) << 5) |
					(blue & 0x1f);
			}
		}
	}
#endif

	// -- Terminate
	close(fb0fd);
	close(fb2fd);

	return 0;
}
