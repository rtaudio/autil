#include "test.h"

#include <random>
#include <limits>
#include <thread>
#include <algorithm>
#include <cmath>

namespace autil {

	uint32_t  test::Rz = 0;
	uint32_t  test::Rw = 0;

	const float PI = std::atan(1.0f) * 4;

	void test::generateMusic(float *buf, int len, bool initalSilence)
	{
		int32_t a1, b1;
		int32_t c1, d1;
		int32_t i, j;
		a1 = b1 = 0;
		c1 = d1 = 0;
		j = 0;
		i = 0;
		if (initalSilence) {
			/*60ms silence*/
			for (; i < 2880; i++)buf[i] = 0;
		}
		for (; i < len; i++)
		{
			uint32_t r;
			int32_t v1;
			v1 = (((j*((j >> 12) ^ ((j >> 10 | j >> 12) & 26 & j >> 7))) & 128) + 128) << 15;
			r = fastRand(); v1 += r & 65535; v1 -= r >> 16;
			b1 = v1 - a1 + ((b1 * 61 + 32) >> 6); a1 = v1;
			c1 = (30 * (c1 + b1 + d1) + 32) >> 6; d1 = b1;
			v1 = (c1 + 128) >> 8;
			buf[i] = (float)(v1 > 32767 ? 32767 : (v1 < -32768 ? -32768 : v1)) / 32768.0f;
			if (i % 6 == 0)j++;
		}
	}

	void test::generateNoise(float *buf, int len) {
		for (int i = 0; i < len; i++) {
			buf[i] = static_cast <float> (fastRand()) / static_cast <float> (std::numeric_limits<uint32_t>::max()) * 2.0f - 1.0f;
		}
	}


	void test::fastRandInit() {
		auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
		uint32_t iseed = (uint32_t)time(NULL) ^ ((tid & 65535) << 16);
		Rw = Rz = iseed;
	}

	void test::generateSweep(float *buf, int len) {
		const float bw = 2.0f / 12.0f;
		const float fMin = 5.0f, fMax = 20000.0f;
		const float sr = 44100;

		float f1 = fMin * std::pow(2.0f, -bw);
		float f2 = (std::min)(fMax * std::pow(2.0f, bw), sr / 2);
		float L = (float)(len - 1) / std::log(f2 / f1);

		for (int i = 0; i < len; i++) {
			buf[i] = std::sin(2.0f * PI *f1* L / sr * (std::exp(((float)i) / L) - 1.0f));
		}

		// fade out using hann window
		int fadeLen = std::round(100.0f * sr / f2 + 2.0f);
		for (int i = 0; i < fadeLen; i++) {
			float h = std::cos(PI *0.5f * ((float)i) / (float)(fadeLen - 1));
			buf[len - fadeLen + i] *= h*h;
		}
	}
}
