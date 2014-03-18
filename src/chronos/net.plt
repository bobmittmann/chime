#set terminal wxt
set terminal windows
#set terminal png size 2048,768 font 'Verdana,12'
#set output './rtc_time.png'
set offset graph 0.0,0.0,0.0,0.0
set autoscale x
# labels
set xlabel 'Time(s)'
set ylabel 'rtc_time'
# define axis
set style line 11 lc rgb '#808080' lt 1
set border 3 back ls 11
set tics out nomirror
set yrange[1.6:1.9] noreverse
set ytics 0.025
# define grid
set style line 12 lc rgb '#808080' lt 0 lw 1
set grid back ls 12
# line styles
set style line 1 lc rgb '#c02020' pt 0 lt 1 lw 2
set style line 2 lc rgb '#20c020' pt 0 lt 1 lw 2
set style line 3 lc rgb '#2020c0' pt 0 lt 1 lw 2
set style line 4 lc rgb '#e08010' pt 0 lt 1 lw 2
set style line 5 lc rgb '#c0c0f0' pt 0 lt 1 lw 2
set style line 6 lc rgb '#c040c0' pt 0 lt 1 lw 2
set style line 7 lc rgb '#808080' pt 0 lt 1 lw 2
set style line 8 lc rgb '#804080' pt 0 lt 1 lw 2
set style line 9 lc rgb '#80f0f0' pt 0 lt 1 lw 2

# './rtc_time.dat' using 1:2 with lp ls 2 title "RTC Time", \

plot './rtc_clk.dat' using 1:2 with lp ls 5 title "RTC Clk", \
	'./master_clk.dat' using 1:2 with lp ls 3 title "Master Clk", \
	'./slave_clk.dat' using 1:2 with lp ls 1 title "Slave Clk", \
	'./pll_err.dat' using 1:2 with lp ls 6 title "PLL Err", \
	'./pll_offs.dat' using 1:2 with lp ls 4 title "PLL Offs", \
	'./filt_offs.dat' using 1:2 with lp ls 7 title "Offs", \
	'./filt_avg.dat' using 1:2 with lp ls 8 title "Avg", \
	'./filt_sigma.dat' using 1:2 with lp ls 9 title "Disp"

quit

pause 2
reread

