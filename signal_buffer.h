#pragma once
#include <stdint.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include<rtt/rtt.h>

#include "signal_processor.h"

#define WITH_DEBUG_NET 1

namespace autil {
	class UdpSocket;
}

void normalize(float *vector, int len);
inline void normalize(std::vector<float> *vector) {
	normalize(vector->data(), vector->size());
}

void medianfilter(float* signal, float* result, int N);
void denoise(float *vector, int len);

float absmax(const float *vector, int len);
float absmax(const std::vector<float> &vector, size_t *index = nullptr);
float energy(const std::vector<float> &vector);

struct SignalDelay {

};

class SignalBuffer {
public:
	std::string name;

	float *m_timeQueue;
	uint32_t m_timeQueuePointer, m_timePreProcessorPos;

	float *m_timeStage;

	float *m_freq;
	uint32_t size, channels;

	uint32_t delay;

	//ITAFFT *m_fft;

	autil::UdpSocket *debugSocket;


	std::vector<SignalProcessor*> m_preProcessors;
	

	inline float * getPtrTQ(uint32_t c) {
		return &m_timeQueue[(size + 0)*c];
	}

	inline float * getPtrTQ(uint32_t c, uint32_t offset) {
		if (offset < 0) {
			offset += size;
		}
		return &m_timeQueue[(size + 0)*c]+ offset;
	}

	inline float * getPtrTS(uint32_t c) {
		return &m_timeStage[(size + 1)*c];
	}


	inline float * getPtrF(uint32_t c) {
		return &m_freq[(size + 1) * 2 * c]; // complex!
	}

	void init() {
		debugSocket = 0;
		m_timeQueuePointer = 0;
		//m_fft = 0;
	}

	SignalBuffer(const std::string &name, uint32_t nChannels, uint32_t size, uint32_t delay = 0)
		: name(name), size(size), channels(nChannels), delay(delay) {
		init();

		m_timeQueue = new float[nChannels*(size + delay + 0)];
		m_timeStage = new float[nChannels*(size + 1)];

		m_freq = new float[nChannels*(size + 1) * 2]; // complex!

		memset(m_timeQueue, 0, nChannels*(size + delay + 0)*sizeof(float));
		memset(m_freq, 0, nChannels*(size + 1) * 2 * sizeof(float));
	}

	SignalBuffer(float *samples, int len) : size(len), channels(1)
	{
		init();

		m_timeQueue = new float[1 * (len + 1)];
		m_timeStage = NULL;
		memcpy(m_timeQueue, samples, len*sizeof(float));
	}

	SignalBuffer(std::vector<float *> samples, int len) : size(len), channels(samples.size())
	{
		init();

		m_timeQueue = new float[samples.size() * (len + 1)];
		m_timeStage = NULL;

		int c = 0;
		for (auto ch : samples) {
			if(ch)
				memcpy(getPtrTQ(c), ch, len*sizeof(float));
			else
				memset(getPtrTQ(c), 0, len*sizeof(float));
			c++;
		}
	}

	// returns number of samples until full
	void addBlock(uint32_t channel, float *block, uint32_t length) {
		if (channel >= channels)
			throw std::out_of_range("Invalid channel number!");

		if (length > size || length == 0)
			throw std::out_of_range("Invalid block size!");

		uint32_t untilEnd = size + delay - m_timeQueuePointer;
		bool breakBlock = (length > untilEnd);

		if (breakBlock) {
			// wrap around: need to copy in two ops
			memcpy(&getPtrTQ(channel)[m_timeQueuePointer], block, untilEnd * sizeof(float));
			memcpy(&getPtrTQ(channel)[0], block + untilEnd, (length - untilEnd) * sizeof(float));
			if (!m_preProcessors.empty()) {
				throw "Unsupported setup: stream preprocess can only be used with signal buffers having a multiple block size length!";
			}

		}
		else {
			memcpy(&getPtrTQ(channel)[m_timeQueuePointer], block, length * sizeof(float));
			if (!m_preProcessors.empty()) {
				uint32_t prev = (m_timeQueuePointer + (size - length)) % size;
				if ((size - prev) < length)
					throw "Unsupported setup: stream preprocess can only be used with signal buffers having a multiple block size length!";
				else
					m_preProcessors[channel]->Process(getPtrTQ(channel, m_timeQueuePointer), length);
				//m_streamPreprocessor->Process(getPtrTQ(channel, prev), getPtrTQ(channel, m_timeQueuePointer), , length);
			}

		}


		// inc pointer with last channel
		if (channel == channels - 1) {
#ifdef WITH_DEBUG_NET
			if (debugSocket && !breakBlock) {
				sendDebugBlock(m_timeQueuePointer, length);
			}
#endif

			m_timeQueuePointer += length;
			m_timeQueuePointer = m_timeQueuePointer % (size + delay);
		}
	}


	void addBlock(uint32_t channel, const uint8_t *srcBlock, const uint32_t srcStride, uint32_t length, void(*converter)(float *out, const uint8_t *in, size_t outStride, size_t n)) {
		if (channel >= channels)
			throw std::out_of_range("Invalid channel number!");

		if (length > size || length == 0)
			throw std::out_of_range("Invalid block size!");

		uint32_t untilEnd = size + delay - m_timeQueuePointer;
		bool breakBlock = (length > untilEnd);

		if (breakBlock) {
			// wrap around: need to copy in two ops
			converter(&getPtrTQ(channel)[m_timeQueuePointer], srcBlock, srcStride, untilEnd );
			converter(&getPtrTQ(channel)[0], srcBlock + untilEnd, srcStride, (length - untilEnd));
			if (!m_preProcessors.empty()) {
				throw "Unsupported setup: stream preprocess can only be used with signal buffers having a multiple block size length!";
			}

		}
		else {
			converter(&getPtrTQ(channel)[m_timeQueuePointer], srcBlock, srcStride, length);
			if (!m_preProcessors.empty()) {
				uint32_t prev = (m_timeQueuePointer + (size - length)) % size;
				if ((size - prev) < length)
					throw "Unsupported setup: stream preprocess can only be used with signal buffers having a multiple block size length!";
				else
					m_preProcessors[channel]->Process(getPtrTQ(channel, m_timeQueuePointer), length);
				//m_streamPreprocessor->Process(getPtrTQ(channel, prev), getPtrTQ(channel, m_timeQueuePointer), , length);
			}

		}


		// inc pointer with last channel
		if (channel == channels - 1) {
#ifdef WITH_DEBUG_NET
			if (debugSocket && !breakBlock) {
				sendDebugBlock(m_timeQueuePointer, length);
			}
#endif

			m_timeQueuePointer += length;
			m_timeQueuePointer = m_timeQueuePointer % (size + delay);
		}
	}

	void getBlock(uint32_t channel, float *block, uint32_t length) {
		if (channel >= channels)
			throw "Invalid channel number!";

		if (length > size || length == 0)
			throw "Invalid block size!";

		uint32_t untilEnd = size - m_timeQueuePointer;


		if (length > untilEnd) {
			// wrap around: need to copy in two ops
			memcpy(block, &getPtrTQ(channel)[m_timeQueuePointer], untilEnd * sizeof(float));
			memcpy(block + untilEnd, &getPtrTQ(channel)[0], (length - untilEnd) * sizeof(float));
		}
		else {
			memcpy(block, &getPtrTQ(channel)[m_timeQueuePointer], length * sizeof(float));
		}

		if (channel == channels - 1) {
			m_timeQueuePointer += length;
			m_timeQueuePointer = m_timeQueuePointer % size;
		}
	}

	void getBlock(uint32_t channel, uint8_t *dstBlock, const uint32_t dstStride, uint32_t length, void (*converter)(uint8_t *out, size_t outStride, const float *in, size_t n)) {
		if (channel >= channels)
			throw "Invalid channel number!";

		if (length > size || length == 0)
			throw "Invalid block size!";

		uint32_t untilEnd = size - m_timeQueuePointer;


		if (length > untilEnd) {
			// wrap around: need to copy in two ops
			converter(dstBlock, dstStride, &getPtrTQ(channel)[m_timeQueuePointer], untilEnd);
			converter(dstBlock + (untilEnd*dstStride), dstStride, &getPtrTQ(channel)[0], (length - untilEnd));
		}
		else {
			converter(dstBlock, dstStride, &getPtrTQ(channel)[m_timeQueuePointer], length);
		}

		if (channel == channels - 1) {
			m_timeQueuePointer += length;
			m_timeQueuePointer = m_timeQueuePointer % size;
		}
	}


	void stage() {
		if (!m_timeStage)
			return;

		for (uint32_t c = 0; c < channels; c++)
		{
			uint32_t readFrom = (size + m_timeQueuePointer) % (size + delay);
			uint32_t readEnd = (readFrom + size) % (size + delay);

			if (readEnd >= readFrom) {
				memcpy(getPtrTS(c), &getPtrTQ(c)[readFrom], size * sizeof(float));
			} else {
				uint32_t untilEnd = size + delay - readFrom;
				memcpy(getPtrTS(c), &getPtrTQ(c)[readFrom], untilEnd*sizeof(float));
				memcpy(getPtrTS(c) + untilEnd, getPtrTQ(c), readEnd*sizeof(float));
			}
		}
	}

	void preProcessTime(int channel)
	{
		// time-domain-pre-processing
		//medianfilter(getPtrT(channel), NULL, size);
		//denoise(getPtrT(channel), size);
		//denoise(getPtrT(channel), size);
		::normalize(getPtrTS(channel), size);
	}

	void preProcessFreq(int channel, bool complexConjugate)
	{
		float * f = getPtrF(channel);


		// freq-domain pre-processing
		uint32_t rampUnti = size / 64;
		uint32_t maskUntil = rampUnti / 2;


		// completely mask [0, size/256]
		for (uint32_t i = 0; i < maskUntil; i++) {
			f[(i * 2) + 0] = 0;
			f[(i * 2) + 1] = 0;
		}


		// linear rampk [size/256, size/6]
		for (uint32_t i = maskUntil; i < size / 128; i++) {
			f[(i * 2) + 0] *= ((float)i - (float)maskUntil) / (float)(rampUnti - maskUntil);
			f[(i * 2) + 1] *= ((float)i - (float)maskUntil) / (float)(rampUnti - maskUntil);
		}

		if (complexConjugate) {
			for (uint32_t i = 0; i < size; i++) {
				f[(i * 2) + 1] = -f[(i * 2) + 1];
			}
		}
	}

	void preProcessorCatchUp()
	{
		//m_timePreProcessorPos
	}

	const float *getFreq(uint32_t channel, uint32_t length, bool complexConjugate = false) {
		if (channel >= channels)
			throw "Invalid channel number!";

		if (length != size)
			throw "Invalid length!";

		preProcessTime(channel);

		throw "No FFT yet!";
		//m_fft->execute(getPtrTS(channel), getPtrF(channel));

		preProcessFreq(channel, complexConjugate);


		return getPtrF(channel);
	}

	void resetIterator() {
		m_timeQueuePointer = 0;
	}

	void normalize() {
		for (uint32_t c = 0; c < channels; c++) {
			::normalize(getPtrTS(c), size);
		}
	}

	
	void setStreamPreprocessor(const SignalProcessor::ProcessFunction &processor)
	{
		for (uint32_t c = 0; c < channels; c++) {
			m_preProcessors.push_back(new SignalProcessor(processor)); // TODO: need to delete!
		}
	}

	int16_t debugBlockIndex;
#ifdef WITH_DEBUG_NET
	void setDebugReceiver(std::string address, int port);
	void sendDebugBlock(uint32_t blockIndex, uint32_t blockLength);
#endif
};

class SignalBufferObserver {
public:
	RttEvent m_evCommit;
	std::vector<SignalBuffer*> m_hists;

	uint32_t updateInterval;
	uint32_t lastUpdate;

	volatile bool commiting;


	SignalBufferObserver(SignalBuffer *buf = NULL) :commiting(false), updateInterval(-1), lastUpdate(-1) {
		if (buf) {
			m_hists.push_back(buf);
			updateInterval = buf->size;
		}
	}

	SignalBufferObserver(const std::vector<SignalBuffer *> &buffers) :commiting(false), updateInterval(-1), lastUpdate(-1) {
		add(buffers);
	}



	SignalBufferObserver(SignalBuffer &buf) :commiting(false), updateInterval(-1), lastUpdate(-1) {
		m_hists.push_back(&buf);
		updateInterval = buf.size;
	}

	SignalBufferObserver(SignalBuffer &buf, SignalBuffer &buf2) :commiting(false), updateInterval(-1), lastUpdate(-1) {
		if (buf.size != buf2.size)
			throw "Cannot observer signals of different sizes!";

		m_hists.push_back(&buf);
		m_hists.push_back(&buf2);
		updateInterval = buf.size;
	}
	
	~SignalBufferObserver() {
		//m_evCommit.Signal();
	}

	void add(const std::vector<SignalBuffer *> &buffers) {
		if (buffers.size() == 0)
			throw std::invalid_argument("Cannot observe an empty vector of SignalBuffers!");

		if(updateInterval == -1)
			updateInterval = buffers[0]->size;

		for (auto b : buffers) {
			if (b->size != updateInterval)
				throw std::invalid_argument("Cannot observe signals of different sizes!");
			m_hists.push_back(b);
		}
	}

	void addHist(SignalBuffer* h) {
		m_hists.push_back(h);
	}

	bool commit() {
		if (commiting)
			return false;

		commiting = true;

		for (auto h : m_hists) {
			h->stage();
		}

		m_evCommit.Signal();
		return true;
	}

	void reset() {
		if (!commiting)
			throw "Called commited() but not commiting!";

		m_evCommit.Reset();

		// why would we need to reset the it?
		//for (auto h : m_hists) {
		//			h->resetIterator();
		//}

		commiting = false;
	}

	void resetBuffers() {
		for (auto h : m_hists) {
			h->resetIterator();
		}
	}
	
	void cancel() {
		commiting = false;
		m_evCommit.Signal();
	}

	bool waitForCommit() {
		if (updateInterval == -1)
			throw "Waiting for a signal with undefined interval!";
		return m_evCommit.Wait() && commiting;
	}


	std::vector<const float*> getTimeStageAll() const
	{
		std::vector<const float*> allData;

		for (auto h : m_hists) {
			for (uint32_t ci = 0; ci < h->channels; ci++) {
				allData.push_back(h->getPtrTS(ci));
			}
		}

		return allData;
	}



	void normalizeSignals()
	{
		for (auto h : m_hists) {
			h->normalize();
		}
	}

	uint32_t getLength() const {
		uint32_t minSize;
		std::for_each(m_hists.begin(), m_hists.end(), [&minSize](SignalBuffer *sb) { if (sb->size < minSize) minSize = sb->size; });
		return minSize;
	}
};


