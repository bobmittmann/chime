clear all

%
% Load the signal package
%
pkg load signal miscellaneous

global FS;
global FC;
global BR;

% Carrier frequency
FC = 4800;
% 
Oversample = 4;
% Sampling frequency
FS = FC * Oversample;
% Nyquist frequency
FN = FS / 2;
% Sampling period
TS = 1 / FS;
% Bit rate
BR = FC / 6;

printf('FS=%dHz FC=%dHz BR=%dbps\n', FS, FC, BR);
printf('Samples/Bit=%d\n', FS / BR);

% modulate a sequence of 32 bits
[fir, n] = bpsk_filt();

% modulate a sequence of bits
x = [silence(6) ...
	bpsk_modulate([ 1 1 1 1 1 1 1 0  1 0 1 0  0 1 0 1  1 1 0 0  0 1 0 1 ]) ...
	silence(8)] * .5;

% band pass filter
v = fftfilt(fir, x);

w = xmit(v, 120, -6); % 120 dg, -12dB SNR
%w = clip(w, [-1, 1]);

y = fftfilt(fir, w); % 30 dg, 12dB SNR
%y = w;

%u = dephase(y, 30); % 30 dg

function [o_p o_q r e h] = pll(x)
	global FS;
	global FC;
	global BR;

	bitlen = FS/BR;
	wlen = length(x);
	qlen = (FS/FC) / 4; % 90dg
	FN = FS/2;

	% phase signal
	xp = x;
	% quadrature signal
	xq(1:qlen) = 0;
	xq(qlen + 1: wlen) = xp(1: wlen - qlen);

	theta = (FC/FS) * 2 * pi;

	k = sin(theta);

	A = [ sqrt(1 - k**2)  k, 
	  -k sqrt(1 - k**2) ];

	% initial oscillator state
%	s = [ sin(phi) cos(phi) ]';
	o_s = [ 0 sqrt(2)/2 ]';
	n = length(x);

	locked = 0;
	lock_cnt = 0;
	lock_tm = 6 * bitlen;
	pll_k = 0.5; % pll convergence parameter

	syncd = 0;

	B = [ 1 0, 
	      0 1 ];

	bittm = 0;
	bit_cnt = 0;
	bit = 1;

	avg = 0;
	r(1: n) = 0;
	h(1: n) = 0;
	for i = 1:n
		o_p(i) = o_s(1); % oscillator in-pahse
		o_q(i) = o_s(2); % oscillator quadrature

		o_s = A * B * o_s; % Update the oscillator state 
		% Oscillator AGC
		o_pwr = o_s(2)**2 + o_s(1)**2; % calculate the power
		o_g = 3/2 - o_pwr; % get the scaling factor
		o_s = o_s * o_g; % scale the state variables

		sin_alfa = o_q(i)*xp(i) + o_p(i)*xq(i); % angle error
		dem = o_p(i)*xp(i) - o_q(i)*xq(i);
		avg = (avg + dem) / 2;

		ki = sin_alfa * pll_k; 
		kq = (1 - (ki**2) / 2);

		if (!locked) 

			B = [ kq ki, 
			     -ki kq ];

			if (avg < 0) 
				lock_cnt= 0;
			else
				lock_cnt++;
			endif

			if (lock_cnt == lock_tm)
				B = [ 1 0, 
				 	  0 1 ];
				locked = 1;
				syncd = 0;
			endif
		else
			lvl = sign(avg);
%			o_s = double(int32(round(o_s * 32768))) / 32768;
			if (!syncd)
				if (lvl < 0)
					bittm++;
					if (bittm == (bitlen / 2))
						syncd = 1;
						bittm = 0;
						bit = 0;
					endif
				else
					bittm = 0;
				endif
			else
				bittm++;
				if (bittm == bitlen) 
					bittm = 0;
					if (lvl < 0)
						bit = 0;
					else
						bit = 1;
					endif
					bit_cnt++; 
					if (bit_cnt == 16)
						locked = 0;
					endif	
					printf(' %d', bit);
				endif
			endif
		endif

%		r(i) = (locked * 0.25) + (syncd * 0.25);
		r(i) = locked;
		e(i) = dem;
		h(i) = bit;

	endfor

	printf('\n');
	
endfunction

N = length(x);
% Simulation end time
TE = length(x) * TS;
t = 0:TS:TE - TS;

[p q r e h] = pll(y);

figure(1);
subplot(3, 1, 1);
plot(t, x);
grid on;

subplot(3, 1, 2);
plot(t, w);
grid on;

subplot(3, 1, 3);
plot(t, y);
grid on;

g = y;

[b, a] = butter(1, (FC / 1)/ FN);
z = filter(b, a, e);

%plot(t, p, t, y, t, r, 'r', t, z, 'c');
figure(2);
subplot(1, 1, 1);
plot(t, y, 'g', t, z, 'r', t, sign(z) .* r * 0.5, 'b', t, h,'m');
grid on;

figure(3);
subplot(1, 1, 1);
plot(t, p, t, sign(z) .* r * 0.5, 'r');
grid on;


