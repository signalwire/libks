ARG BASE_IMAGE=signalwire/freeswitch-base:debian-10
FROM ${BASE_IMAGE}

RUN apt-get update && apt-get install -yq build-essential autotools-dev lsb-release pkg-config automake autoconf libtool-bin clang-tools-7
RUN apt-get install -yq cmake uuid-dev libssl-dev

RUN mkdir -p /sw/libks

WORKDIR /sw/libks

COPY . .

RUN sed -i -e 's/GIT_FOUND AND GZIP_CMD AND DATE_CMD/WE_DO_NOT_WANT_A_DEBIAN_PACKAGE_THANK_YOU/' CMakeLists.txt && cmake . -DCMAKE_INSTALL_PREFIX=/usr -DWITH_LIBBACKTRACE=1 && make install