
``` bash
mkdir -p build
cd build
cmake -DCMAKE_C_FLAGS="" ..
```

build with -DDEBUG for debug messages
``` bash
cmake -DCMAKE_C_FLAGS="-DDEBUG" ..
```

Example of running - 2 workers calculating integral from 0 to 100 (now function is static - f(x) = x)
``` bash
make
./worker > worker1_log.txt 2>&1 & sleep 1; ./worker > worker2_log.txt 2>&1 & sleep 1; ./master 0 100 > master_log.txt 2>&1
killall worker
killall master
```

