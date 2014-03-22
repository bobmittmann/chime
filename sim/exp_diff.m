clear all


n = 0:256;

x0 = 1;
y0 = x0;

kx = 8;
ky = 32;

x = x0 * (1  - 1 / kx) .** n;
y = x0 * (1  - 1 / ky) .** n;

z = y - x;
plot(n, x, n, y, n, z);

function z = xfe(M, N, x0, i)
	x = x0 * (1  - 1 / M) .** i;
	y = x0 * (1  - 1 / M) .** i - x0 * (1 / M - 1 / N * M) .** i;
	z = (y - x .* (1  - 1 / N)) / M;
endfunction

function z = yfe(M, N, x0, n)
	x(1) = x0;
	y(1) = x0;
	z(1) = 0;

	for i = 1:length(n) - 1
		x(i + 1) = x(i) - x(i) / N;
		z(i + 1) = (y(i) - x(i + 1)) / M;
		y(i + 1) = y(i) - z(i + 1);
	endfor
endfunction

function z = fe(M, N, x0, n)
	x(1) = x0;
	y(1) = x0;
	z(1) = 0;

	for i = 1:length(n) - 1
		z(i + 1) = (y(i) - (x(i) - x(i) / N)) / M;
		y(i + 1) = y(i) - z(i + 1);
		x(i + 1) = x(i) - x(i) / N;
	endfor
endfunction

x0 = 1;
N = 16;
x1 = fe(2, N, x0, n);
x2 = fe(4, N, x0, n);
x3 = fe(8, N, x0, n);
x4 = fe(16, N, x0, n);
x5 = fe(32, N, x0, n);
x6 = fe(64, N, x0, n);
x7 = fe(128, N, x0, n);
x8 = fe(256, N, x0, n);

plot(n, x1, n, x2, n, x3, n, x4, n, x5, n, x6, n, x7, n, x8);


