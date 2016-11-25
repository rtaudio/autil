#pragma once

#include <string>
#include <vector>

class SignalBufferObserver;
class SignalBuffer;

namespace autil {
	
//struct sockaddr_in;



class UdpSocket {
private:
	void *soc;
	struct sockaddr_in *sa;
	short blockIndex_;
public:
	UdpSocket(std::string receiverAddress, int port);
	~UdpSocket();
	bool Send(const char *data, int length);
	bool Send(const SignalBufferObserver& observer);
	//bool Send(const std::vector<float>& samples);
	bool Send(const std::vector<const std::vector<float>*> &samples);

	bool sendBlock(const std::vector<const float*> &samples, size_t blockSize, int16_t blockIndex);
};
}