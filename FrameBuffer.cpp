#include "FrameBuffer.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <linux/fb.h>

#include "Exception.h"

FrameBuffer::FrameBuffer(const char* deviceName)
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

FrameBuffer::~FrameBuffer()
{
	int r = munmap(data, length);
	if (r != 0)
	{
		throw Exception("munmap failed.");
	}

	close(fd);
}


void FrameBuffer::WaitForVSync()
{
	int io = ioctl(fd, FBIO_WAITFORVSYNC, 0);
	if (io < 0)
	{
		throw Exception("FBIO_WAITFORVSYNC failed.");
	}

}