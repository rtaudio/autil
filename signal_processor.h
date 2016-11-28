#pragma once

#include<functional>
#include <stdint.h>

class SignalProcessor
{
	static const int MaxBlockSize = 1024 * 8;
public:
	typedef std::function<void(const float*blockInPrev, const float*blockIn, float *blockOut, uint32_t blockSize)> ProcessFunction;

	SignalProcessor(const ProcessFunction &process);
	virtual ~SignalProcessor();

	void Process(float *block, uint32_t blockSize);

private:
	ProcessFunction process;
	float prevBlock[MaxBlockSize];
};


template<unsigned int L>
float heapMedian3(const float *a)
{
	static const int	Lh = (int)(L / 2) + 1;

	const float  *p;
	float left[Lh], right[Lh], median;
	unsigned char nLeft, nRight;

	// pick first value as median candidate
	p = a;
	median = *p++;
	nLeft = nRight = 1;

	for (;;)
	{
		// get next value
		float val = *p++;

		// if value is smaller than median, append to left heap
		if (val < median)
		{
			// move biggest value to the heap top
			unsigned char child = nLeft++, parent = (child - 1) / 2;
			while (parent && val > left[parent])
			{
				left[child] = left[parent];
				child = parent;
				parent = (parent - 1) / 2;
			}
			left[child] = val;

			// if left heap is full
			if (nLeft == Lh)
			{
				// for each remaining value
				for (unsigned char nVal = L - (p - a); nVal; --nVal)
				{
					// get next value
					val = *p++;

					// if value is to be inserted in the left heap
					if (val < median)
					{
						child = left[2] > left[1] ? 2 : 1;
						if (val >= left[child])
							median = val;
						else
						{
							median = left[child];
							parent = child;
							child = parent * 2 + 1;
							while (child < Lh)
							{
								if (child < (Lh - 1) && left[child + 1] > left[child])
									++child;
								if (val >= left[child])
									break;
								left[parent] = left[child];
								parent = child;
								child = parent * 2 + 1;
							}
							left[parent] = val;
						}
					}
				}
				return median;
			}
		}

		// else append to right heap
		else
		{
			// move smallest value to the heap top
			unsigned char child = nRight++, parent = (child - 1) / 2;
			while (parent && val < right[parent])
			{
				right[child] = right[parent];
				child = parent;
				parent = (parent - 1) / 2;
			}
			right[child] = val;

			// if right heap is full
			if (nRight == Lh)
			{
				// for each remaining value
				for (unsigned char nVal = L - (p - a); nVal; --nVal)
				{
					// get next value
					val = *p++;

					// if value is to be inserted in the right heap
					if (val > median)
					{
						child = right[2] < right[1] ? 2 : 1;
						if (val <= right[child])
							median = val;
						else
						{
							median = right[child];
							parent = child;
							child = parent * 2 + 1;
							while (child < Lh)
							{
								if (child < (Lh - 1) && right[child + 1] < right[child])
									++child;
								if (val <= right[child])
									break;
								right[parent] = right[child];
								parent = child;
								child = parent * 2 + 1;
							}
							right[parent] = val;
						}
					}
				}
				return median;
			}
		}
	}
}