#include <iostream>

#include "signal_buffer.h"

#ifdef WITH_DEBUG_NET
#include "net.h"
#endif



#ifdef WITH_DEBUG_NET
	void SignalBuffer::setDebugReceiver(std::string address, int port) {

		std::cout << "Added debug UDP stream from SignalBuffer [" << name << "] to " << address << ":" << port << "!" << std::endl;
		debugSocket = new autil::UdpSocket(address, port);
		debugBlockIndex = 0;
	}
#endif

#ifdef WITH_DEBUG_NET

#if 1

	void SignalBuffer::sendDebugBlock(uint32_t blockIndex, uint32_t blockLength) {
		std::vector<const float *> samples;
		for (size_t ci = 0; ci < channels; ci++) {
			samples.push_back(getPtrTQ(ci, blockIndex));
		}		
		debugSocket->sendBlock(samples, blockLength, debugBlockIndex);		
		debugBlockIndex++; // this will wrap arround, -127->128
	}

#else

	void SignalBuffer::sendDebugBlock(uint32_t blockIndex, uint32_t blockLength) {
		// header: [index|#channels|blockLength|1|ch0_norm|ch1_norm...|chN_norm]
		int headerLen = 4 * sizeof(int16_t) + sizeof(int16_t)*channels;

		uint32_t dataBytes = headerLen + (blockLength * channels * sizeof(int16_t));
		int16_t *data = new int16_t[dataBytes / sizeof(int16_t)];

		data[0] = (int16_t)debugBlockIndex;
		data[1] = (int16_t)channels;
		data[2] = (int16_t)blockLength;
		data[3] = (int16_t)1;



		for (int ci = 0; ci < channels; ci++) {
			float cmax = absmax(getPtrTQ(ci, blockIndex), blockLength);
			if (cmax > 1.0f || cmax < -1.0f) {
				delete[] data;
				return;
			}
			data[4 + ci] = (int16_t)(cmax * 0x7fff);
			for (uint32_t i = 0; i < blockLength; i++) {
				float f = (cmax == 0.0f) ? 0.0f : (getPtrTQ(ci, blockIndex)[i] / cmax);
				if (f > 1.0f) f = 1.0f;
				if (f < -1.0f) f = -1.0f;
				data[headerLen / sizeof(int16_t) + ci * blockLength + i] = (int16_t)(f * 0x7fff);
			}
		}

		if (!debugSocket->Send((const char *)data, dataBytes))
			printf("failed to send debug data!\n");
		delete[] data;

		// this will wrap arround, 32,767->-32,678
		debugBlockIndex++;
	}

#endif
#endif


	float absmax(const float *vector, int len)
	{
		float max = 0;

		for (int i = 0; i < len; i++) {
			float a = std::abs(vector[i]);
			if (a > max)
				max = a;
		}
		return max;
	}


	float absmax(const std::vector<float> &vector, size_t *index)
	{
		float max = 0, a;
		auto n = vector.size();
		size_t iMax;

		for (size_t i = 0; i < n; i++) {
			a = std::abs(vector[i]);
			if (a > max) {
				max = a;
				iMax = i;
			}
		}

		if (index) *index = iMax;

		return max;
	}

	float energy(const std::vector<float> &vector)
	{
		auto n = vector.size();
		float v;
		double energy = 0.0;
		for (size_t i = 0; i < n; i++) {
			v = vector[i];
			energy += v*v;
		}
		return (float)energy;
	}




	void normalize(float *vector, int len) {
		float max = absmax(vector, len);

		if (max == 0.0f || max == 1.0f)
			return;

		float q = 1.0f / max;

		for (int i = 0; i < len; i++) {
			vector[i] = vector[i] * q;
		}
	}


	void denoise(float *vector, int len) {
		int filterLen = 4;

		for (int i = 0; i < len; i++) {
			float m = 0;
			for (int j = i - filterLen + 1; j <= i; j++) {
				m += (j < 0) ? 0 : vector[j];
			}
			m /= (float)filterLen;
			vector[i] = m;
		}
	}



	//   1D MEDIAN FILTER implementation
	//     signal - input signal
	//     result - output signal
	//     N      - length of the signal
	void _medianfilter(const float* signal, float* result, int N)
	{
		//   Move window through all floats of the signal
		for (int i = 2; i < N - 2; ++i)
		{
			//   Pick up window floats
			float window[5];
			for (int j = 0; j < 5; ++j)
				window[j] = signal[i - 2 + j];
			//   Order floats (only half of them)
			for (int j = 0; j < 3; ++j)
			{
				//   Find position of minimum float
				int min = j;
				for (int k = j + 1; k < 5; ++k)
					if (window[k] < window[min])
						min = k;
				//   Put found minimum float in its place
				const float temp = window[j];
				window[j] = window[min];
				window[min] = temp;
			}
			//   Get result - the middle element
			result[i - 2] = window[2];
		}
	}

	//   1D MEDIAN FILTER wrapper
	//     signal - input signal
	//     result - output signal
	//     N      - length of the signal
	void medianfilter(float* signal, float* result, int N)
	{
		//   Check arguments
		if (!signal || N < 1)
			return;
		//   Treat special case N = 1
		if (N == 1)
		{
			if (result)
				result[0] = signal[0];
			return;
		}
		//   Allocate memory for signal extension
		float* extension = new float[N + 4];
		//   Check memory allocation
		if (!extension)
			return;
		//   Create signal extension
		memcpy(extension + 2, signal, N * sizeof(float));
		for (int i = 0; i < 2; ++i)
		{
			extension[i] = signal[1 - i];
			extension[N + 2 + i] = signal[N - 1 - i];
		}
		//   Call median filter implementation
		_medianfilter(extension, result ? result : signal, N + 4);
		//   Free memory
		delete[] extension;
	}




	void _medianfilterCausal(const float* signal, float* result, int N)
	{
		//   Move window through all floats of the signal
		for (int i = 2; i < N - 2; ++i)
		{
			//   Pick up window floats
			float window[5];
			for (int j = 0; j < 5; ++j)
				window[j] = signal[i - 2 + j];
			//   Order floats (only half of them)
			for (int j = 0; j < 3; ++j)
			{
				//   Find position of minimum float
				int min = j;
				for (int k = j + 1; k < 5; ++k)
					if (window[k] < window[min])
						min = k;
				//   Put found minimum float in its place
				const float temp = window[j];
				window[j] = window[min];
				window[min] = temp;
			}
			//   Get result - the middle element
			result[i - 2] = window[2];
		}
	}

	//   1D MEDIAN FILTER wrapper
	//     signal - input signal
	//     result - output signal
	//     N      - length of the signal
	void medianfilterCausal(float* signal, float* result, int N)
	{
		if (!signal || N < 1)
			return;

		if (N == 1)
		{
			result[0] = signal[0];
			return;
		}

		//   Allocate memory for signal extension
		float* extension = new float[N + 4];
		if (!extension)
			return;

		//   Create signal extension
		memcpy(extension + 2, signal, N * sizeof(float));
		for (int i = 0; i < 2; ++i)
		{
			extension[i] = signal[1 - i];
			extension[N + 2 + i] = signal[N - 1 - i];
		}
		//   Call median filter implementation
		_medianfilter(extension, result ? result : signal, N + 4);
		//   Free memory
		delete[] extension;
	}



