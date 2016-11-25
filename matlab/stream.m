portNums = 25025 + [1];
fftOrder = 11;

sbr = SignalBufferReceiver(portNums,2^fftOrder);

timeData = [];
while isempty(timeData)
    timeData = sbr.receive();
end

[maxPeak, peakI] = max(abs(timeData));

timeDataMasked = timeData;
timeDataMasked(round(peakI-0.5*48):round(peakI+0.5*48)) = 0;

C_spec = abs(fft(timeData));
C_spec = C_spec(1:end/2);
C_spec = C_spec(1:round(end*0.8));
C_spec = C_spec .^ 2;

C_spec  = C_spec -1.3;

specEnergy = sum(C_spec);
varEnergy = sum(abs(C_spec - mean(C_spec)));

figure;
plot(C_spec);
ylim([0, max(C_spec)*1.1]);

flatness = 1 - (varEnergy / specEnergy)

%flatness = nthroot(prod(C_spec),size(C_spec,1)) / ( sum(C_spec) / size(C_spec,1))

%C_spec(400:700) = C_spec(400:700) *1.1;

flatness = exp( 1/size(C_spec,1) * sum(log(C_spec))) / ( sum(C_spec) / size(C_spec,1))


plot(timeDataMasked);
%set(gca, 'YScale', 'log')
