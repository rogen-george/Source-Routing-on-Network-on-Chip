# Gnuplot script file for plotting data in file "uniform_traffic.dat"
set title "4x4 mesh with uniform traffic"
set xlabel "injection rate"
set ylabel "avg. latency"
plot [0:1] [1:1500] "uniform_traffic.dat" using 1:2 with linespoints title "dor", \
"uniform_traffic.dat" using 1:3 with linespoints title "oddeven", \
"uniform_traffic.dat" using 1:4 with linespoints title "oddeven vc based selection", \
"uniform_traffic.dat" using 1:5 with linespoints title "oddeven num-of-flits", \
"uniform_traffic.dat" using 1:6 with linespoints title "oddeven num-of-flits per port"

