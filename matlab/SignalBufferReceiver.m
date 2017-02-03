classdef SignalBufferReceiver
    properties
        bufSize
        hudprs
        timeBuffer
        prevBlockIndex
        preDelayBuffer
    end
    methods
        function obj = SignalBufferReceiver(portNums, bufSize)
            if nargin < 2
                bufSize = 2^11;
            end
            
            if nargin < 1 || ~isnumeric(portNums) || ~isnumeric(bufSize)
                error('Need to pass port number(s) and buffer size to contructor');
            end
            
            obj.bufSize = bufSize;
            obj.hudprs = cell(length(portNums),1);
            for pi = 1:length(portNums)
                hudpr = dsp.UDPReceiver;
                hudpr.MessageDataType = 'int8';
                hudpr.MaximumMessageLength = 65507;
                hudpr.ReceiveBufferSize = 1024*256;
                hudpr.LocalIPPort = portNums(pi);
                obj.hudprs{pi} = hudpr;
            end
            obj.prevBlockIndex = -1;
            obj.timeBuffer = zeros(bufSize, length(obj.hudprs)*2);
        end
        
        function delete(obj)
            for pi = 1:length(obj.hudprs)
                release(obj.hudprs{pi});
            end
        end
        
        function [ timeDataOut, timeStampOut ] = receive(obj)
            [blockData, bi] = obj.receive_block();
            bi = bi + 32767;
            
            
            if bi ~= obj.prevBlockIndex + 1 && obj.prevBlockIndex ~= -1
                fprintf('lost blocks (got %d, expected %d)\n', bi, obj.prevBlockIndex);
                obj.prevBlockIndex = bi -1;
            end
            
            timeStampOut = obj.prevBlockIndex*size(blockData,1);
            %numchannels = size(blockData,2);
            
            if size(obj.timeBuffer,1) > size(blockData,1) 
                obj.timeBuffer = [obj.timeBuffer((size(blockData,1)+1):end,:); blockData];
                timeDataOut = obj.timeBuffer;
            else
                timeDataOut = blockData;
            end        
        end
        
        
        function [ timeDataOut, blockIndexOut] = receive_block( obj )
            preDelay = 0; % num of blocks the signal will be delayed
            portsToRead = 1:length(obj.hudprs);
            timeDataOut = [];
            blockIndexOut = -1;
            numPorts = length(obj.hudprs);
            
            for pi = portsToRead
                fprintf('waiting for data on port %d\n', obj.hudprs{pi}.LocalIPPort);
            end
            
            while ~isempty(portsToRead) > 0
                pause(0.001);
                for pi = portsToRead
                    dataReceived = step(obj.hudprs{pi});
                    if isempty(dataReceived)
                        continue;
                    end
                    
                    bi = dataReceived(1);
                    numChannels = int32(dataReceived(2));
                    blockLength = 2^int32(dataReceived(3))
                    % dataReceived(4) unused (always 1)
                    
                    % sync
                    if blockIndexOut > 0 && bi ~= blockIndexOut
                        if bi < blockIndexOut && (double(blockIndexOut-bi)*blockLength) < 48000*2  % just re-read until blockIndex
                            fprintf('out-of-sync old package on %d (got: %d, expected: %d), dropping\n', pi, bi, blockIndexOut);
                            continue;
                        else
                            % we have to restart on all other ports :-(
                            fprintf('out-of-sync future package on %d (got: %d, expected: %d) , restart others\n', pi, bi, blockIndexOut);
                            blockIndexOut = bi;
                            portsToRead = 1:length(hudprs);
                            portsToRead = portsToRead(portsToRead~=pi);
                            continue;
                        end
                    end
                    
                    portsToRead = portsToRead(portsToRead~=pi);
                    blockIndexOut = bi;
                    
                    
                    channelNorms = zeros(numChannels,1);
                    
                    timeDataOut = zeros(blockLength, numChannels*numPorts);
                    
                    if isempty(obj.preDelayBuffer) || size(obj.preDelayBuffer,1) ~= (preDelay*blockLength)
                        obj.preDelayBuffer = zeros(preDelay*blockLength, numChannels*numPorts);
                    end
                    
                    for ci = 1:numChannels
                        cn = dataReceived(4+ci);
                        if(cn < 0)
                            cn = double(cn) + 256;
                        end
                        channelNorms(ci) = double(cn) / 255.0;
                        rss = 4+numChannels + blockLength*(ci-1) + (1:blockLength);
                        timeDataOut(:,ci + numChannels*(pi-1)) = double(dataReceived(rss)) / 128.0 * channelNorms(ci);
                    end
                end
            end
            
            if preDelay > 0
                obj.preDelayBuffer = [obj.preDelayBuffer((size(timeDataOut,1)+1):end,:); timeDataOut];
                timeDataOut(:,:) = obj.preDelayBuffer(1:size(timeDataOut,1),:);
            end
        end
    end
end
