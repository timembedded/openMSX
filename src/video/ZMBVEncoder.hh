// $Id$

// Code based on DOSBox-0.65

#ifndef ZMBVENCODER_HH
#define ZMBVENCODER_HH

#include <vector>
#include <zlib.h>

namespace openmsx {

class FrameSource;

class ZMBVEncoder
{
public:
	static const char* CODEC_4CC;

	ZMBVEncoder(unsigned width, unsigned height, unsigned bpp);
	~ZMBVEncoder();

	void compressFrame(bool keyFrame, FrameSource* frame,
	                   void*& buffer, unsigned& written);

private:
	enum Format {
		ZMBV_FORMAT_15BPP = 5,
		ZMBV_FORMAT_16BPP = 6,
		ZMBV_FORMAT_32BPP = 8
	};

	void setupBuffers(unsigned bpp);
	unsigned neededSize();
	template<class P> void addXorFrame();
	template<class P> unsigned possibleBlock(int vx, int vy, unsigned offset);
	template<class P> unsigned compareBlock(int vx, int vy, unsigned offset);
	template<class P> void addXorBlock(int vx, int vy, unsigned offset);
	template<class P> void lineBEtoLE(unsigned char* input, unsigned width);
	const void* getScaledLine(FrameSource* frame, unsigned y);

	unsigned char* oldframe;
	unsigned char* newframe;
	unsigned char* work;
	unsigned char* output;
	unsigned outputSize;
	unsigned workUsed;

	std::vector<unsigned> blockOffsets;
	z_stream zstream;

	const unsigned width;
	const unsigned height;
	unsigned pitch;
	unsigned pixelSize;
	Format format;
};

} // namespace openmsx

#endif
