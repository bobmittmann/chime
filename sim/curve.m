clear all


y(1) = 1;
x = -16:1/16:16;

m = length(x) - 1;
k = 1;
%for n = 1:length(x) - 1
%
%	y(n + 1) = n/m *(y(n) - k/n);
%%	k = k * k;
%endfor

%y = (1 -  x.**2 ./ (x.**2 + 1));

%y =  x ./ (x - 1);
y =  atan(x) .* 2 ./ pi;

z(1) = y(1);

for n = 1:length(x) - 1
	z(n + 1) = z(n) + y(n);
endfor

plot(x, y, x, z / 128);

