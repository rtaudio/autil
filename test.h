#pragma once

#include <cstdint>


namespace autil {

class test
{
public:
	static inline uint32_t fastRand(void)
	{
		if (!Rz && !Rw) fastRandInit();
		Rz = 36969 * (Rz & 65535) + (Rz >> 16);
		Rw = 18000 * (Rw & 65535) + (Rw >> 16);
		return (Rz << 16) + Rw;
	}

	static void fastRandInit();

	static void generateMusic(float *buf, int len, bool inititalSilence = false);
	static void generateNoise(float *buf, int len);

	static void generateSweep(float *buf, int len, float samplingRate);

private:
	static uint32_t Rz, Rw;
	
	test() {};
};
}