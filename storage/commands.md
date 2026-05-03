# Distributed database with replicas. RAFT protocol for consensus task solving

## Start

```bash
rm -rf data/*
go run ./cmd/dbsim -nodes 3 -base-port 18080 -data data
```

## Get node status

```bash
curl -s http://127.0.0.1:18080/status
curl -s http://127.0.0.1:18081/status
curl -s http://127.0.0.1:18082/status
```

get leader addr from here


## Create

### leader

```bash
curl -i -X PUT http://127.0.0.1:18082/items/user-1 \
  -H 'Content-Type: application/json' \
  -d '{"name":"bob","balance":20,"version":1}'
```

expect OK

### follower

```bash
curl -i -X PUT http://127.0.0.1:18080/items/from-follower \
  -H 'Content-Type: application/json' \
  -d '{"written":"through redirect"}'
```

expect redirect

```text
HTTP/1.1 307 Temporary Redirect
Location: http://127.0.0.1:18082/items/from-follower
```

try with auto-redirecting

```bash
curl -i -L -X PUT http://127.0.0.1:18080/items/from-follower\
  -H 'Content-Type: application/json'\
  -d '{"written":"through redirect"}'
```


## Read

### leader

```bash
curl -i http://127.0.0.1:18082/items/user-1
```

expect location of follower to read

### follower

```bash
curl -i http://127.0.0.1:18080/items/user-1
```

good read

## Update

```bash
curl -i -X PATCH http://127.0.0.1:18082/items/user-1\
  -H 'Content-Type: application/json'\
  -d '{"balance":25}'
```

check with

```bash
curl -i http://127.0.0.1:18080/items/user-1
```

## CAS

### succ

```bash
curl -i -X POST http://127.0.0.1:18082/cas/user-1\
  -H 'Content-Type: application/json'\
  -d '{"expected":{"version":1,"balance":25,"name":"bob"},"update":{"name":"bob","balance":26,"version":2}}'
```

check resp for

```json
"swapped":true
```

### fail
```bash
curl -i -X POST http://127.0.0.1:18082/cas/user-1\
  -H 'Content-Type: application/json'\
  -d '{"expected":{"name":"bob","balance":25,"version":1},"update":{"name":"bob","balance":27,"version":3}}'
```

check resp for

```json
"swapped":false
```

check data

```bash
curl -i http://127.0.0.1:18080/items/user-1
```

## Delete

### CAS delete


```bash
curl -i -X POST http://127.0.0.1:18082/cas/user-1 \
  -H 'Content-Type: application/json' \
  -d '{"expected":{"name":"bob","balance":26,"version":2},"update":null}'
```


check:

```bash
curl -i http://127.0.0.1:18080/items/user-1
```
exepect

```text
HTTP/1.1 404 Not Found
```

than we can create with CAS


```bash
curl -i -X POST http://127.0.0.1:18082/cas/user-1\
  -H 'Content-Type: application/json'\
  -d '{"expected":null,"update":{"name":"bob","balance":30,"version":1}}'
```


## fail and recover


```bash
curl -i -X POST http://127.0.0.1:18080/admin/fail
```

node inactive


write smth

```bash
curl -i -X PUT http://127.0.0.1:18082/items/while-follower-down-1 \
  -H 'Content-Type: application/json' \
  -d '{"n":1}'

curl -i -X PUT http://127.0.0.1:18082/items/while-follower-down-2 \
  -H 'Content-Type: application/json' \
  -d '{"n":2}'
```

check failed 

```bash
curl -s http://127.0.0.1:18080/status
```

recover failed node

```bash
curl -i -X POST http://127.0.0.1:18080/admin/recover
sleep 1
```

check statuses for last log

```bash
curl -s http://127.0.0.1:18080/status
curl -s http://127.0.0.1:18081/status
curl -s http://127.0.0.1:18082/status
```


## leader fail 

get leader addr (here 18082)

```bash
curl -s http://127.0.0.1:18082/status
```

crash leader

```bash
curl -i -X POST http://127.0.0.1:18082/admin/fail
sleep 2
```


check for new leader

```bash
curl -s http://127.0.0.1:18080/status
curl -s http://127.0.0.1:18082/status
```

try do smth

```bash
curl -i -L -X PUT http://127.0.0.1:18080/items/after-leader-fail \
  -H 'Content-Type: application/json' \
  -d '{"leader":"reelected"}'
```


return old leader
```bash
curl -i -X POST http://127.0.0.1:18082/admin/recover
sleep 1
```

```bash
curl -s http://127.0.0.1:18080/status
curl -s http://127.0.0.1:18081/status
curl -s http://127.0.0.1:18082/status
```
check old leader is follower as it is outdated
