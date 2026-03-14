FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    make \
    cmake \
    iproute2 \
    iputils-ping \
    net-tools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY app/ .

RUN mkdir -p build && cd build && \
    cmake -DCMAKE_C_FLAGS="-DENV" .. && \
    make -j$(nproc)

EXPOSE 6000/tcp
EXPOSE 4000/udp

COPY docker/master/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
