clear all

%
% Load the signal package
%
pkg load signal

global TS;
global FS;
global FC;
global BR;

% Sampling period
TS = 32;
% Sampling frequency
FS = 1 / TS;
% Nyquist frequency
FN = FS / 2;

printf('FS=%9.6fHz, TS=%d sec\n', FS, 1/FS);

% Jitter frequency filter
FC = FN / 2;

printf('FC=%9.6fHz, TC=%d sec\n', FC, 1/FC);

%[b, a] = butter(1, FC / FN);
[b, a] = besself(1, FC / FN);

a
b

[h,w] = freqz(b,a);
figure(1);
plot(w/(2*pi),abs(h));

