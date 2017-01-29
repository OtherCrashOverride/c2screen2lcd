#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <linux/fb.h>


// Ion video header from drivers\staging\android\uapi\ion.h
#include "ion.h"
#include "meson_ion.h"
#include "ge2d.h"
#include "ge2d_cmd.h"

#include "IonBuffer.h"
#include "ge2d.h"
#include "ge2d_cmd.h"

#include <memory>
#include <string>


class FrameBuffer
{
	std::string deviceName;
	int fd;
	int width;
	int height;
	int bpp;
	int length;
	void* data;


public:

	const std::string& DeviceName() const
	{
		return deviceName;
	}

	int FileDescriptor() const
	{
		return fd;
	}

	int Width() const
	{
		return width;
	}

	int Height() const
	{
		return height;
	}

	int BitsPerPixel() const
	{
		return bpp;
	}

	int Length() const
	{
		return length;
	}

	void* Data() const
	{
		return data;
	}



	FrameBuffer(const char* deviceName)
	{
		if (deviceName == nullptr)
		{
			throw Exception("bad device name");
		}


		this->deviceName = deviceName;


		fd = open(deviceName, O_RDWR);
		if (fd < 0)
		{
			throw Exception("open failed.");
		}

		
		int io;
		struct fb_var_screeninfo info;

		io = ioctl(fd, FBIOGET_VSCREENINFO, &info);
		if (io < 0)
		{
			throw Exception("FBIOGET_VSCREENINFO failed.");
		}


		width = info.xres;
		height = info.yres;
		bpp = info.bits_per_pixel;
		length = width * height * (bpp / 8);


		data = mmap(0, length, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
		if (data == MAP_FAILED)
		{
			throw Exception("mmap failed");
		}
	}

	~FrameBuffer()
	{
		int r = munmap(data, length);
		if (r != 0)
		{
			throw Exception("munmap failed.");
		}

		close(fd);
	}
};



int main()
{
	int io;


	// -- open fb0
	FrameBuffer fb0("/dev/fb0");
	
	printf("fb0: screen info - width=%d, height=%d, bpp=%d\n", fb0.Width(), fb0.Height(), fb0.BitsPerPixel());


	// -- open fb2
	FrameBuffer fb2("/dev/fb2");

	printf("fb2: screen info - width=%d, height=%d, bpp=%d\n", fb2.Width(), fb2.Height(), fb2.BitsPerPixel());


	// GE2D
	int ge2d_fd = open("/dev/ge2d", O_RDWR);
	if (ge2d_fd < 0)
	{
		throw Exception("open /dev/ge2d failed.");
	}


	// Ion
	IonBuffer lcdBuffer(fb2.Length());

	void* lcdBufferPtr = mmap(NULL,
		lcdBuffer.Length(),
		PROT_READ | PROT_WRITE,
		MAP_FILE | MAP_SHARED,
		lcdBuffer.ExportHandle(),
		0);
	if (!lcdBufferPtr)
	{
		throw Exception("lcdBufferPtr mmap failed.");
	}


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


	// Color spaces:
	//   LCD = RGB565
	//   HDMI = ARGB32


	// Configure GE2D
	struct config_para_ex_s configex = { 0 };

	switch (fb0.BitsPerPixel())
	{
		case 16:
			configex.src_para.format = GE2D_FORMAT_S32_ARGB;
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


	ge2d_para_s blitRect = { 0 };

	blitRect.src1_rect.x = 0;
	blitRect.src1_rect.y = 0;
	blitRect.src1_rect.w = fb0.Width();
	blitRect.src1_rect.h = fb0.Height();

	blitRect.dst_rect.x = 0;
	blitRect.dst_rect.y = 0;
	blitRect.dst_rect.w = fb2.Width();
	blitRect.dst_rect.h = fb2.Height();


	while (true)
	{
		// Wait for VSync
		io = ioctl(fb0.FileDescriptor(), FBIO_WAITFORVSYNC, 0);
		if (io < 0)
		{
			throw Exception("FBIO_WAITFORVSYNC failed.");
		}

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
