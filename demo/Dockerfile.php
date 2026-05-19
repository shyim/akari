FROM php:8.4-apache

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
    && docker-php-ext-enable akari \
    && rm -rf /usr/src/akari

# Configure
RUN { \
    echo "akari.enable=1"; \
    echo "akari.service_name=demo-php-app"; \
    echo "akari.udp_host=forwarder"; \
    echo "akari.udp_port=4319"; \
    echo "akari.max_depth=16"; \
} >> /usr/local/etc/php/conf.d/docker-php-ext-akari.ini

# Copy demo app
COPY demo/app/ /var/www/html/
WORKDIR /var/www/html
