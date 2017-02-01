#pragma once

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
	void* physicalAddress;

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

	void* PhysicalAddress() const
	{
		return physicalAddress;
	}


	FrameBuffer(const char* deviceName);
	~FrameBuffer();


	void WaitForVSync();
	bool GetTransparencyEnabled();
};
