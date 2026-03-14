# Docker-окружение для master/worker

## Структура

```
docker/
├── docker-compose.yml          # 1 master + 3 worker в bridge-сети
├── config/
│   ├── master.env              # MASTER_PORT, DISCOVERY_PORT, BROADCAST_ADDR
│   └── worker.env              # WORKER_PORT, DISCOVERY_PORT, MASTER_PORT
├── master/
│   ├── Dockerfile
│   └── entrypoint.sh           # запускает ./master 0 100
├── worker/
│   ├── Dockerfile
│   └── entrypoint.sh           # запускает ./worker
```

## Сборка и запуск

Все команды выполняются из `integral/docker`.

Собирает образы, создаёт сеть, поднимает 1 master + 3 worker:

```bash
docker compose up --build
```

Или раздельно:

```bash
# сборка образа
docker compose build

# запуск в фоне
docker compose up -d

# логи
docker compose logs -f

# остановка и удаление контейнеров + сети
docker compose down
```

Количество воркеров можно менять:

```bash
docker compose up --build --scale worker=5
```

## Конфигурация

Переменные окружения (задаются в `config/*.env` или через `-e`):

| Переменная       | Где            | По умолчанию   | Описание                                      |
|------------------|----------------|-----------------|-----------------------------------------------|
| `MASTER_PORT`    | master, worker | `6000`          | TCP-порт для раздачи задач                    |
| `DISCOVERY_PORT` | master, worker | `4000`          | UDP-порт для broadcast-обнаружения            |
| `BROADCAST_ADDR` | master         | `255.255.255.255` | Адрес broadcast (в Docker — адрес подсети)  |
| `WORKER_PORT`    | worker         | `5000`          | Порт воркера                                  |

`BROADCAST_ADDR` нужен потому, что Docker bridge-сети не пропускают broadcast на `255.255.255.255`. Используется broadcast-адрес подсети (`172.20.0.255` для `172.20.0.0/24`).

