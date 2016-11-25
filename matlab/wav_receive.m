function [ timeData, timeStamp ] = wav_receive( hudprs, bufsize )
persistent timeBuffer;
persistent prevBlockIndex;
[blockData, bi] = wav_receive_block(hudprs);
bi = bi + 32767;

if isempty(timeBuffer) || size(timeBuffer,1) ~= bufsize
     timeBuffer = zeros(bufsize, length(hudprs)*2);    % stereo
     prevBlockIndex = bi-1;
end
   
if bi ~= prevBlockIndex + 1
    fprintf('lost blocks (got %d, expected %d)\n', bi, prevBlockIndex);
    prevBlockIndex = bi -1;
end

timeStamp = prevBlockIndex*size(blockData,1);
    
%numchannels = size(blockData,2);

timeBuffer = [timeBuffer((size(blockData,1)+1):end,:); blockData];
    
timeData = timeBuffer;

for ci = 1:size(timeData,2)
    timeData(:,ci) = timeData(:,ci) -  medfilt1(timeData(:,ci), 48);
    %timeData(:,ci) = timeData(:,ci) -  medfilt1(timeData(:,ci), 48/4-1);
    timeData(:,ci) = timeData(:,ci) ./ max(timeData(:,ci));   
end    


end

function [ timeData, blockIndex] = wav_receive_block( hudprs )
persistent preDelayBuffer;
preDelay = 3;

portsToRead = 1:length(hudprs);

timeData = [];

blockIndex = -1;


while ~isempty(portsToRead) > 0
    for pi = portsToRead 
        
        
        dataReceived = step(hudprs{pi});
        if isempty(dataReceived)
            continue;
        end
       
        
        
        bi = dataReceived(1);
        blockLength = dataReceived(3);
         
        % sync
        if blockIndex > 0 && bi ~= blockIndex
            if bi < blockIndex && (double(blockIndex-bi)*blockLength) < 48000*2  % just re-read until blockIndex
                 fprintf('out-of-sync old package on %d (got: %d, expected: %d), dropping\n', pi, bi, blockIndex);
                continue;
            else
                % we have to restart on all other ports :-(
                fprintf('out-of-sync future package on %d (got: %d, expected: %d) , restart others\n', pi, bi, blockIndex);
                blockIndex = bi;
                portsToRead = 1:length(hudprs);
                portsToRead = portsToRead(portsToRead~=pi);
                continue;
            end
        end
        
        portsToRead = portsToRead(portsToRead~=pi);
        
        blockIndex = bi;
        
        numChannels = dataReceived(2);
       
        
        channelNorms = zeros(numChannels,1);
        
        if isempty(timeData)
            timeData = zeros(blockLength, numChannels*length(hudprs));
        end
        
        if isempty(preDelayBuffer) || size(preDelayBuffer,1) ~= (preDelay*blockLength)
            preDelayBuffer = zeros(preDelay*blockLength, numChannels*length(hudprs));
        end
        
        for ci = 1:numChannels
            channelNorms(ci) = double(dataReceived(4+ci)) / 32767.0;
            rss = 4+numChannels + blockLength*(ci-1) + (1:blockLength);
            timeData(:,ci + numChannels*(pi-1)) = double(dataReceived(rss)) / 32767.0 * channelNorms(ci);
        end
        
    end

end

    
    preDelayBuffer = [preDelayBuffer((size(timeData,1)+1):end,:); timeData];
    timeData(1:end,1:2) = preDelayBuffer(1:size(timeData,1),1:2);
    
end
