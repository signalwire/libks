ARG BUILDER_IMAGE=arm64v8/debian:bookworm
ARG MAINTAINER="Andrey Volk <andrey@signalwire.com>"

FROM ${BUILDER_IMAGE} AS builder

ARG BUILD_NUMBER=42
ARG GIT_SHA=0000000000

MAINTAINER ${MAINTAINER}

SHELL ["/bin/bash", "-c"]

RUN apt-get -q update && \
    DEBIAN_FRONTEND=noninteractive apt-get -yq install \
        apt-transport-https \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        debhelper \
        devscripts \
        dh-autoreconf \
        dos2unix \
        doxygen \
        git \
        graphviz \
        libglib2.0-dev \
        libssl-dev \
        lsb-release \
        pkg-config \
        wget

RUN update-ca-certificates --fresh

ENV DATA_DIR=/data
WORKDIR ${DATA_DIR}

COPY . ${DATA_DIR}
RUN git reset --hard HEAD && git clean -xfd

RUN git config --global --add safe.directory '*'

# Bootstrap and Build
RUN PACKAGE_RELEASE="${BUILD_NUMBER}.${GIT_SHA}" cmake . \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_INSTALL_PREFIX="/usr" && \
    make package && mkdir OUT && mv -v *.deb OUT/.

# Artifacts image (mandatory part, the resulting image must have a single filesystem layer)
FROM scratch
COPY --from=builder /data/OUT/ /
