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

printf('FS=%9.6fHz\n', FS);

% Simulation end time
%TE = 60 * 60 % 1 hour
%t = 0:TS:TE - TS;
%N = length(t);

%dt = TS/N;
%r = 0:dt:TS - dt;

% +-50ms random jitter
%x = unifrnd(TS -0.050, TS + 0.050, N, 1);
%x = normrnd(TS, 0.050, N, 1);
%x = normrnd(0, 0.050, N, 1) + r';

% Jitter frequency filter
FC = FN / 4;

printf('FC=%9.6fHz, %d sec\n', FC, 1/FC);

b = [ 0.029955,
  0.059909,
  0.029955,
  ];

a = [ 1.0,
  -1.0,
  0.57406 ];


b = [ 0.07945793732498336600,
	0.15891587464996673000,
	0.07945793732498336600 ];

a = [ 1.00000000000000000000,
	-1.53291006779006710000,
	0.83765643185306482000 ];

b = [ 0.11505882726663068000,
  0.23011765453326136000,
  0.11505882726663068000];

a = [ 1.00000000000000000000,
  -0.98657618056564034000,
  0.44681268983470201000 ];

[b, a] = butter(1, FC / FN);
%[b, a] = cheby2(2, 12, FC / FN);
%[b, a] = besself(5, FC / FN);

%[h,w] = freqz(b,a);
%figure(1);
%plot(w/(2*pi),abs(h));

%
% Load data files
%
off = load('../src/chronos/offset02.dat');
err = load('../src/chronos/error02.dat');
t = off(:,1);
x = off(:,2);
e = err(:,2);


% compute intial state of the filter 
xi = ones(128, 1) * TS;
xi = zeros(128, 1);
[yi, si] = filter(b, a, xi);

% do filtering 
[y, sf] = filter(b, a, x, si);

% derivative
x0 = shift(x, 1);
dx = x - x0;

y0 = shift(y, 1);
dy = y - y0;

[dz, sf] = filter(b, a, dy);

%plot(t, dx, 'c', t, dy, 'r', t, de, 'b');
%plot(t, x, 'c', t, y, 'r', t, e, 'b');

plot(t, x, 'c', t, y, 'r', t, dy, 'g');
