#if WIN32
#define _WINSOCKAPI_ 
#include <windows.h>
#include <winsock2.h>
#include <Ws2tcpip.h>
#pragma comment( lib, "Ws2_32.lib" )
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>	
#define SOCKET int
#endif

#include "net.h"
#include "logging.h"
#include "signal_buffer.h"

namespace autil {

	UdpSocket::UdpSocket(std::string receiverAddress, int port) {
		blockIndex_ = 0;
#if WIN32
		static bool needWSInit = true;

		if (needWSInit) {
			WSADATA wsa;
			WSAStartup(MAKEWORD(2, 0), &wsa);
			needWSInit = false;
		}
#endif

		int r;

		SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (s == -1) {
			throw "Failed to create UDP socket!";
		}



		int send_buffer = 16 * 1024;
		int send_buffer_sizeof = sizeof(send_buffer);
		r = setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char*)&send_buffer, send_buffer_sizeof);
		if (r < 0) {
			throw ("Could not set send buffer size!");
		}

        soc = s;


		sa = new struct sockaddr_in();
		memset(sa, 0, sizeof(struct sockaddr_in));
		sa->sin_family = AF_INET;
		r = inet_pton(AF_INET, receiverAddress.c_str(), &(sa->sin_addr));
		if (r <= 0) {
			throw ("Invalid address " + receiverAddress);
		}
		sa->sin_port = htons(port);


		LOG(logDEBUG) << "Created UDP port to " << receiverAddress << ":" << port;
	}

	UdpSocket::~UdpSocket() {
		delete sa;
	}

	bool UdpSocket::Send(const char *data, int length) {
		int r = sendto((SOCKET)soc, data, length, 0, (struct sockaddr*)sa, sizeof(struct sockaddr_in));
		return (r == length);
	}


	bool UdpSocket::Send(const SignalBufferObserver& observer) {
		auto floatData = observer.getTimeStageAll();
		uint32_t len = 64; // observer.getLength();
		int headerLen = (1 + 1 + 1 + 1) * sizeof(uint32_t) / sizeof(int16_t);

		int16_t *data = new int16_t[floatData.size() * len + headerLen];



		*((uint32_t*)(data)+0) = floatData.size(); // num channels
		*((uint32_t*)(data)+1) = len; // length
		*((uint32_t*)(data)+2) = observer.lastUpdate; // length
		*((uint32_t*)(data)+3) = 0; // not in use

		int ci = 0;
		for (auto cd : floatData) {
			for (uint32_t i = 0; i < len; i++) {
				float f = floatData[ci][i];
				if (f > 1.0) f = 1.0;
				if (f < -1.0) f = -1.0;
				data[headerLen + ci * len + i] = (int16_t)(f * 0x7fff);
			}
			ci++;
		}
		auto res = Send((const char *)data, (floatData.size() * len + headerLen) * sizeof(*data));

		delete[] data;

		return res;
	}

	/*
	bool UdpSocket::Send(const std::vector<float>& samples)
	{
		return sendBlock({ samples.data() }, samples.size(), blockIndex_++);
	}*/

	bool UdpSocket::Send(const std::vector<const std::vector<float>*> &samples)
	{
		auto blockSize = samples[0]->size();
		std::vector<const float *> ps(samples.size());

		for (auto s : samples) {
			if (s->size() != blockSize)
				throw std::invalid_argument("Channels must have equal length! (" + std::to_string(s->size()) + " vs " + std::to_string(blockSize) + ")");
			ps.push_back(s->data());
		}
		return sendBlock(ps, blockSize, blockIndex_++);
	}

	bool UdpSocket::sendBlock( const std::vector<const float*> &samples, size_t blockSize, int16_t blockIndex)
	{
		// header: [index|#channels|blockLength|1|ch0_norm|ch1_norm...|chN_norm]
		int headerLen = 4 * sizeof(int8_t) + sizeof(int8_t)*samples.size();

		uint32_t dataBytes = headerLen + (blockSize * samples.size() * sizeof(int8_t));
		int8_t *data = new int8_t[dataBytes / sizeof(int8_t)];

		int bl = blockSize, logBl = 0;
		while (bl >>= 1) { ++logBl; }

		data[0] = (int8_t)blockIndex;
		data[1] = (int8_t)samples.size();
		data[2] = (int8_t)logBl;
		data[3] = (int8_t)1;

		for (size_t ci = 0; ci < samples.size(); ci++) {
			float cmax = absmax(samples[ci], blockSize);
			if (cmax > 1.0f || cmax < -1.0f) {
				LOG(logERROR) << "failed to send debug data: sample value out of [-1,1]";
				delete[] data;
				return false;
			}
			data[4 + ci] = (int8_t)(cmax * 0xff);
			for (uint32_t i = 0; i < blockSize; i++) {
				float f = (cmax == 0.0f) ? 0.0f : (samples[ci][i] / cmax);
				if (f > 1.0f) f = 1.0f;
				if (f < -1.0f) f = -1.0f;
				data[headerLen / sizeof(int8_t) + ci * blockSize + i] = (int8_t)(f * 0x7f);
			}
		}

		bool res = Send((const char *)data, dataBytes);

		if (!res) {
			LOG(logERROR) << "failed to send debug data!";
		}

		delete[] data;

		return res;
	}

}
