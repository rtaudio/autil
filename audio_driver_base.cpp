#include <vector>
#include <fstream>
#include <string>
#include <string.h>
#include <iostream>

#include "audio_driver_base.h"
#include "signal_buffer.h"
#include "test.h"

namespace autil {
AudioDriverBase::AudioDriverBase(const std::string &name) :
    m_totalFramesProcessed(0),
    m_running(false),
    m_newActions(false),
    m_paused(false),
    m_name(name)
{
    memset(m_buffers, 0, sizeof(m_buffers));
    memset(m_bufferPool, 0, sizeof(m_bufferPool));
}


AudioDriverBase::~AudioDriverBase()
{
    m_running = false;
}

void AudioDriverBase::pauseAudioProcessing()
{
    m_paused = true;
    m_newActions = true;
    m_evtActionQueueProcessed.Wait();
}

void AudioDriverBase::addObserver(SignalBufferObserver *pool)
{
    _uniquePtrArrayAdd((void**)m_bufferPool, MAX_SIGNAL_BUFFERS, pool);
    pool->lastUpdate = -1;
}

void AudioDriverBase::removeSignal(SignalBuffer *buffer)
{
    int ib = _uniquePtrArrayIndexOf((void**)m_buffers, MAX_SIGNAL_BUFFERS, buffer);
    if (ib < 0)
        throw std::runtime_error("Tried to remove unknown SignalBuffer");
    m_buffers[ib] = nullptr;

    for (uint32_t c = 0; c < buffer->channels; c++) {
        auto con = getBufferPortConnection(ib, c);
        if (con->port)      
			signalPortDestroy(con->port);

        con->port = nullptr;
        con->isOutput = false;
    }
}

void AudioDriverBase::removeObserver(SignalBufferObserver *observer)
{
    int io = _uniquePtrArrayIndexOf((void**)m_bufferPool, MAX_SIGNAL_BUFFERS, observer);
    if (io < 0)
        throw std::runtime_error("Tried to remove unknown SignalBufferObserver");
    m_bufferPool[io] = nullptr;
}

int AudioDriverBase::_uniquePtrArrayAdd(void **array, int len, void* ptr)
{
    int iEmpty = 0;
    for (int i = (len - 1); i >= 0; i--) {
        if (array[i] == ptr)
            return -1;
        if (array[i] == NULL)
            iEmpty = i;
    }

    array[iEmpty] = ptr;
    return iEmpty;
}

int AudioDriverBase::_uniquePtrArrayIndexOf(void **array, int len, void* ptr)
{
    for (int i = (len - 1); i >= 0; i--) {
        if (array[i] == ptr)
            return i;
    }

    return -1;
}

void AudioDriverBase::processActionQueueInAudioThread() {
    if (m_newActions) {
        //printf("AudioDriver: processing queue...\n");

        while (m_actionQueue.size()) {
            try {
                //std::cout << "proc..." << std::endl;
                m_actionQueue.front()();
                //std::cout << "done!" << std::endl;
            }
            catch (const std::exception &ex) {
                std::cout << "AudioDriver exception:" << ex.what() << std::endl;
            }
            m_actionQueue.pop();
        }


        // remove cleared streamers
        //m_streamers.erase(std::remove_if(m_streamers.begin(), m_streamers.end(),
        //                                     [](SignalStreamer *s) { return !!s->func || (s->in == s->out); }), ad->m_streamers.end());

        m_newActions = false;
        m_evtActionQueueProcessed.Signal();
    }
}

void AudioDriverBase::processSignalBufferObserverInAudioThread(uint32_t nframes) {
    // signal buffer observers
    for (int ip = 0; ip < MAX_SIGNAL_BUFFERS; ip++) {
        auto bufferPool = m_bufferPool[ip];

        if (!bufferPool)
            continue;

        if (bufferPool->lastUpdate == -1)
            bufferPool->lastUpdate = m_totalFramesProcessed;

        if (bufferPool->updateInterval >= 0 && (m_totalFramesProcessed - bufferPool->lastUpdate) > bufferPool->updateInterval) {
            bufferPool->lastUpdate = m_totalFramesProcessed;
            if (!bufferPool->commit()) {
                printf("History comit failed! Update thread is too slow.\n");
                bufferPool->lastUpdate += bufferPool->updateInterval; // add penalty time
            }
        }
    }

    m_totalFramesProcessed += nframes;
}

void AudioDriverBase::addSignal(SignalBuffer *buffer, const std::vector<BufferPortConnection> &ports)
{
    int ib = _uniquePtrArrayAdd((void**)m_buffers, MAX_SIGNAL_BUFFERS, buffer);

    if (ib == -1)
        throw std::runtime_error("Signal buffer already added!");

    bool isPlayback = ports[0].isOutput;

    for (uint32_t c = 0; c < buffer->channels; c++) {
        auto con = getBufferPortConnection(ib, c);
        *con = ports[c];
    }

    buffer->resetIterator();
}


}
