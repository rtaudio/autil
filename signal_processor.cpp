#include "signal_processor.h"

#include <string.h>

SignalProcessor::SignalProcessor(const ProcessFunction &process) : process(process)
{
	memset(prevBlock, 0, sizeof(prevBlock));
}


SignalProcessor::~SignalProcessor()
{
}



void SignalProcessor::Process(float *block, uint32_t blockSize)
{
	if (blockSize > MaxBlockSize)
		throw "Block size too big!";

	// concat prev block with current
	float cur2Blocks[MaxBlockSize*2];
	memcpy(cur2Blocks, prevBlock, blockSize*sizeof(float));
	memcpy(cur2Blocks+ blockSize, block, blockSize*sizeof(float));
	memcpy(prevBlock, block, blockSize*sizeof(float));

	process(cur2Blocks, cur2Blocks + blockSize, block, blockSize);

}
