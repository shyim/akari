FROM webdevops/php-apache:8.5

RUN apt-get update && apt-get install -y \
    libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy extension source
WORKDIR /usr/src/akari
COPY config.m4 ./
COPY include/ ./include/
COPY src/ ./src/

# Build and install
RUN phpize \
    && ./configure --enable-akari \
    && make -j$(nproc) \
    && make install \
    && rm -rf /usr/src/akari

# Configure
RUN { \
    echo "extension=akari.so"; \
    echo "akari.enable=1"; \
    echo "akari.service_name=demo-php-app"; \
    echo "akari.udp_host=forwarder"; \
    echo "akari.udp_port=4319"; \
    echo "akari.max_depth=16"; \
} >> /usr/local/etc/php/conf.d/docker-php-ext-akari.ini

COPY demo/app/ /app
