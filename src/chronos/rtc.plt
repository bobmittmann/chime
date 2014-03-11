set terminal wxt
#set terminal png size 1280,768 font 'Verdana,12'
set output './rtc_time.png'
set offset graph 0.0,0.0,0.0,0.0
# labels
set xlabel 'Time(s)'
set ylabel 'rtc_time'
# define axis
set style line 11 lc rgb '#808080' lt 1
set border 3 back ls 11
set tics out nomirror
# define grid
set style line 12 lc rgb '#808080' lt 0 lw 1
set grid back ls 12
# line styles
set style line 1 lc rgb '#c01010' pt 0 lt 1 lw 2
set style line 2 lc rgb '#10c010' pt 0 lt 1 lw 2
set style line 3 lc rgb '#1010c0' pt 0 lt 1 lw 2
set style line 4 lc rgb '#f04010' pt 0 lt 1 lw 2
plot './rtc_time.dat' using 1:2 notitle with lp ls 2, \
	'./rtc_clk.dat' using 1:2 notitle with lp ls 3, \
	'./master_clk.dat' using 1:2 notitle with lp ls 4
set output
quit
