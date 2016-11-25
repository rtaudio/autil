#pragma once

#include <vector>
#include <queue>

#include <functional>

#if defined(_WIN32) && !defined(WIN32)
//#include <Windows.h>
typedef void* jack_native_thread_t;
#endif
//#define WIN32

#include <jack/jack.h>


#include <rtt/rtt.h>

#include "signal_buffer.h"
#include "signal_processor.h"

namespace autil {
	struct SignalStreamer {
		

		std::function<bool(const float * in, float * out, size_t nframes)> func;
		jack_port_t *in;
		jack_port_t *out;
		inline SignalStreamer(std::function<bool(const float * in, float * out, size_t nframes)> f) : in(0), out(0), func(f) { }
		void waitForEnd() { endEvent.Wait(); }

		SignalStreamer(const SignalStreamer& other) = delete;
		SignalStreamer& operator=(const SignalStreamer&) = delete;
		SignalStreamer(SignalStreamer&& other) = default;
		SignalStreamer& operator=(SignalStreamer&& other) = default;
	private:
		friend class AudioDriver;

		


		RttEvent endEvent;
		void _end() { endEvent.Signal(); }
	};

	class AudioDriver
	{
	public:

		typedef std::vector<const char *> PortArray;

		enum class Connect : int {
			ToCapture = 2 << 0,
			ToPlayback = 2<<1,

			ToPlaybackSource = 2 << 2,
		};


		class RequestProgress {
			SignalBufferObserver obs;
			RequestProgress(std::vector<SignalBuffer *> buffers) {
				if (buffers.size()) {

				}
				for (auto b : buffers)
					obs.addHist(b);
			}

			void wait() {
				//if(obs.waitForCommit)
			}

			void cleanup() {

			}
		};

		class Request {
			typedef std::function<void()> Action;

			AudioDriver *driver;
			std::queue<Action> actions;
			std::queue<Action> undoActions;

			std::vector<SignalBuffer *> buffers;

		public:
			Request(AudioDriver *driver) : driver(driver) {}

			Request &addBuffer(SignalBuffer *buffer, Connect connection) {
				auto ports = driver->createSignalPorts(buffer, buffer->name, connection); // can't create ports in audio thread!
				actions.push(std::bind(&AudioDriver::addSignal, driver, buffer, ports));
				undoActions.push(std::bind(&AudioDriver::removeSignal, driver, buffer));
				buffers.push_back(buffer);
				return *this;
			}
			Request &addObserver(SignalBufferObserver *observer) {
				actions.push(std::bind(&AudioDriver::addObserver, driver, observer));
				undoActions.push(std::bind(&AudioDriver::removeObserver, driver, observer));
				return *this;
			}
			Request &addStreamer(SignalStreamer *streamer, const std::string &name, Connect connection, int channel);

			Request &remove(SignalBuffer *buffer) {
				actions.push(std::bind(&AudioDriver::removeSignal, driver, buffer));
				return *this;
			}
			Request &remove(SignalBufferObserver *observer) {
				actions.push(std::bind(&AudioDriver::removeObserver, driver, observer));
				return *this;
			}


			void execute() {
				RttLocalLock ll(driver->m_mtxActionQueue);

				while (actions.size()) {
					driver->m_actionQueue.push(actions.front());
					actions.pop();
				}
				driver->commit();

				buffers.clear();
			}

			void executeAndObserve(SignalBufferObserver *observer) {
				observer->add(buffers);
				addObserver(observer);
				execute();
			}

			void undo() {
				RttLocalLock ll(driver->m_mtxActionQueue);
				while (undoActions.size()) {
					driver->m_actionQueue.push(undoActions.front());
					undoActions.pop();
				}
				driver->commit();
				buffers.clear();
			}
		};

		AudioDriver(const std::string &name);
		~AudioDriver();

		void pauseAudioProcessing();

		inline int getNumCaptureChannels() const { return m_portsCaptureNum; }
		inline int getNumPlaybackChannels() const { return m_portsPlaybacleNum; }
		inline int getSampleRate() const { return m_sampleRate; }
		inline int getBlockSize() const { return m_blockSize; }
		inline uint32_t getClock() const { return m_totalFramesProcessed; }

		void setBlockSize(int blockSize);


		/*
		inline std::vector<jack_port_t*> NewInput(std::string baseName, int nChannels) {
			return newPorts(baseName, nChannels, false);
		}

		inline std::vector<jack_port_t*> NewOutput(std::string baseName, int nChannels) {
			return newPorts(baseName, nChannels, true);
		}
		*/

		void muteOthers(bool mute = true);

		static const int MAX_SIGNAL_BUFFERS = 16;
		static const int MAX_CHANNELS_PER_BUFFER = 4;
	private:
		std::vector<jack_port_t*> newPorts(std::string baseName, int nChannels, bool outputNotInput);

		std::string m_name;

		// API:
		std::vector<jack_port_t*> createSignalPorts(SignalBuffer *buffer, const std::string &name, Connect connection);
		jack_port_t* createSignalPort( std::string name, Connect connection, uint32_t channel);
		void addSignal(SignalBuffer *buffer, std::vector<jack_port_t*> ports);
		void addObserver(SignalBufferObserver *pool);
		void addStreamer(SignalStreamer *buffer);
		void removeSignal(SignalBuffer *buffer);
		void removeObserver(SignalBufferObserver *buffer);
		void removeStreamer(SignalStreamer &buffer);

		inline void commit(bool async = false) {
			while (m_newActions) {
				m_evtActionQueueProcessed.Wait(m_blockSize * 2000 / m_sampleRate);
			}
			m_evtActionQueueProcessed.Reset();
			m_newActions = true;
			if (!async) {
				m_evtActionQueueProcessed.Wait();
			}
		}

		static int jackProcess(jack_nframes_t nframes, void *arg);
		static void jackShutdown(void *arg);

		jack_client_t* initJack();


		//std::vector<SignalBuffer>

		jack_client_t *m_jackClient;
		int m_sampleRate;
		int m_blockSize;


		const char **m_portsCapture;
		const char **m_portsPlayback;
		int m_portsCaptureNum, m_portsPlaybacleNum;

		std::vector<PortArray> m_mutedPorts;

		std::vector<SignalProcessor*> m_dsps;

		bool m_running, m_paused;
		unsigned long m_totalFramesProcessed;



		

		// we use bare arrays here
		SignalBuffer *m_buffers[MAX_SIGNAL_BUFFERS];
		SignalBufferObserver *m_bufferPool[MAX_SIGNAL_BUFFERS];
		std::vector<SignalStreamer*> m_streamers;


		volatile bool m_newActions;
		RttMutex m_mtxActionQueue;
		RttEvent m_evtActionQueueProcessed;
		std::queue<std::function<void()>> m_actionQueue;

		struct BufferPortConnection {
			jack_port_t *port;
			bool isOutput;
		};

		BufferPortConnection m_bufferPortConnections[MAX_SIGNAL_BUFFERS*MAX_CHANNELS_PER_BUFFER];

		inline BufferPortConnection *getBufferPortConnection(int bufferId, int channel) {
			return &m_bufferPortConnections[MAX_SIGNAL_BUFFERS * bufferId + channel];
		}
	};

	inline constexpr AudioDriver::Connect operator&(AudioDriver::Connect __x, AudioDriver::Connect __y)
	{
		return static_cast<AudioDriver::Connect>(static_cast<int>(__x) & static_cast<int>(__y));
	}

	inline constexpr AudioDriver::Connect
		operator|(AudioDriver::Connect __x, AudioDriver::Connect __y)
	{
		return static_cast<AudioDriver::Connect>
			(static_cast<int>(__x) | static_cast<int>(__y));
	}

}