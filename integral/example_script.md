
``` bash
mkdir -p build
cd build
cmake ..

killall worker
killall master
make
./worker > worker1_log.txt 2>&1 & sleep 1; ./worker > worker2_log.txt 2>&1 & sleep 1; ./master 0 100 > master_log.txt 2>&1
```
