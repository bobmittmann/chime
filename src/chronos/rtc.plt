reset
#set terminal wxt
set terminal windows
#set terminal png size 2048,768 font 'Verdana,12'
#set output './rtc_time.png'
set offset graph 0.0,0.0,0.0,0.0
set autoscale
# labels
set xlabel 'Time(s)'
set ylabel 'Offset(s)'
# define axis
set style line 11 lc rgb '#808080' lt 1
set border 3 back ls 11
set tics out nomirror
# define grid
set style line 12 lc rgb '#808080' lt 0 lw 1
set grid back ls 12
# line styles
set style line 1 lc rgb '#c02020' pt 0 lt 1 lw 2
set style line 2 lc rgb '#20c020' pt 0 lt 1 lw 2
set style line 3 lc rgb '#2020c0' pt 0 lt 1 lw 2
set style line 4 lc rgb '#e08010' pt 0 lt 1 lw 2
set style line 5 lc rgb '#c0c0f0' pt 0 lt 1 lw 2
set style line 6 lc rgb '#80c0c0' pt 0 lt 1 lw 2

plot './rtc_time.dat' using 1:2 with lp ls 2 title "RTC Time", \
	'./rtc_clk.dat' using 1:2 with lp ls 5 title "RTC Clk", \
	'./master_clk.dat' using 1:2 with lp ls 3 title "Master Clk", \
	'./master_temp.dat' using 1:2 with lp ls 4 title "Master Temp", \
	'./fll_win.dat' using 1:2 with lp ls 6 title "FLL Filter" 

quit
pause 2
reread

