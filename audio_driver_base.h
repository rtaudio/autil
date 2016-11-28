#pragma once

#include <vector>
#include <queue>

#include <functional>


#include <rtt/rtt.h>

#include "signal_buffer.h"
#include "signal_processor.h"

namespace autil {
    class AudioDriverBase
	{
	public:

		enum class Connect : int {
			ToCapture = 2 << 0,
			ToPlayback = 2<<1,
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

            AudioDriverBase *driver;
			std::queue<Action> actions;
			std::queue<Action> undoActions;

			std::vector<SignalBuffer *> buffers;

		public:
            Request(AudioDriverBase *driver) : driver(driver) {}

			Request &addBuffer(SignalBuffer *buffer, Connect connection) {
                std::vector<BufferPortConnection> cons;
                bool isPlayback = connection == Connect::ToPlayback;
                for (uint32_t c = 0; c < buffer->channels; c++) {
                    cons.push_back(BufferPortConnection{driver->signalPortNew(connection, c, buffer->name), isPlayback});
                }
                actions.push([this, buffer, cons]() { driver->addSignal(buffer, cons); });

                undoActions.push(std::bind(&AudioDriverBase::removeSignal, driver, buffer));
				buffers.push_back(buffer);
				return *this;
			}
			Request &addObserver(SignalBufferObserver *observer) {
                actions.push(std::bind(&AudioDriverBase::addObserver, driver, observer));
                undoActions.push(std::bind(&AudioDriverBase::removeObserver, driver, observer));
				return *this;
			}
            //Request &addStreamer(SignalStreamer *streamer, const std::string &name, Connect connection, int channel);

			Request &remove(SignalBuffer *buffer) {
                actions.push(std::bind(&AudioDriverBase::removeSignal, driver, buffer));
				return *this;
			}
			Request &remove(SignalBufferObserver *observer) {
                actions.push(std::bind(&AudioDriverBase::removeObserver, driver, observer));
				return *this;
			}


			void execute() {
				RttLocalLock ll(driver->m_mtxActionQueue);

				driver->sync();

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

				driver->sync();

				while (undoActions.size()) {
					driver->m_actionQueue.push(undoActions.front());
					undoActions.pop();
				}
				driver->commit();
				buffers.clear();
			}
		};

        AudioDriverBase(const std::string &name);
        ~AudioDriverBase();



        virtual void pauseAudioProcessing();

        inline int getNumCaptureChannels() const { return m_numChannelsCapture; }
        inline int getNumPlaybackChannels() const { return m_numChannelsPlayback; }
		inline int getSampleRate() const { return m_sampleRate; }
		inline int getBlockSize() const { return m_blockSize; }
		inline uint32_t getClock() const { return m_totalFramesProcessed; }

        virtual void setBlockSize(int blockSize) = 0;

		static const int MAX_SIGNAL_BUFFERS = 16;
		static const int MAX_CHANNELS_PER_BUFFER = 4;
    protected:
        struct BufferPortConnection {
            void *port;
            bool isOutput;
        };


		std::string m_name;

        void addSignal(SignalBuffer *buffer, const std::vector<BufferPortConnection> &cons);

        virtual void *signalPortNew(Connect connection, uint32_t channel, const std::string &name ){ return nullptr; }
        virtual void signalPortDestroy(void *port){};
		// API:        
		void addObserver(SignalBufferObserver *pool);
//		void addStreamer(SignalStreamer *buffer);
		void removeSignal(SignalBuffer *buffer);
		void removeObserver(SignalBufferObserver *buffer);
//		void removeStreamer(SignalStreamer &buffer);

		inline void sync() {
            while (m_newActions && m_running) {
				m_evtActionQueueProcessed.Wait(m_blockSize * 2000 / m_sampleRate);
			}
			m_evtActionQueueProcessed.Reset();
		}
		
		inline void commit(bool async = false) {
			sync();
			m_newActions = true;
            if (!async && m_running) {
				m_evtActionQueueProcessed.Wait();
			}
		}

        void processActionQueueInAudioThread();
        void processSignalBufferObserverInAudioThread(uint32_t nframes);


		int m_sampleRate;
		int m_blockSize;



        int m_numChannelsCapture, m_numChannelsPlayback;


		std::vector<SignalProcessor*> m_dsps;

        volatile bool m_running;
        volatile bool m_paused;
		unsigned long m_totalFramesProcessed;
		

		// we use bare arrays here
		SignalBuffer *m_buffers[MAX_SIGNAL_BUFFERS];
		SignalBufferObserver *m_bufferPool[MAX_SIGNAL_BUFFERS];
//		std::vector<SignalStreamer*> m_streamers;


		volatile bool m_newActions;
		RttMutex m_mtxActionQueue;
		RttEvent m_evtActionQueueProcessed;
		std::queue<std::function<void()>> m_actionQueue;



        BufferPortConnection m_bufferPortConnections[MAX_SIGNAL_BUFFERS*MAX_CHANNELS_PER_BUFFER];

        inline BufferPortConnection *getBufferPortConnection(int bufferId, int channel) {
            return &m_bufferPortConnections[MAX_SIGNAL_BUFFERS * bufferId + channel];
        }


        int _uniquePtrArrayAdd(void **array, int len, void* ptr);
        int _uniquePtrArrayIndexOf(void **array, int len, void* ptr);
	};

    inline constexpr AudioDriverBase::Connect operator&(AudioDriverBase::Connect __x, AudioDriverBase::Connect __y)
	{
        return static_cast<AudioDriverBase::Connect>(static_cast<int>(__x) & static_cast<int>(__y));
	}

    inline constexpr AudioDriverBase::Connect
        operator|(AudioDriverBase::Connect __x, AudioDriverBase::Connect __y)
	{
        return static_cast<AudioDriverBase::Connect>
			(static_cast<int>(__x) | static_cast<int>(__y));
	}

}
