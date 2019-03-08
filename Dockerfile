FROM ubuntu:latest

# Install Basic Components
RUN apt update -y && apt install -y fuse jq wget gcc make golang git curl

# Install genisoimage
RUN apt install -y genisoimage

# Install casync
RUN apt install -y casync

# Install desync
RUN mkdir /go
ENV GOPATH=/go
ENV PATH=$PATH:${GOPATH}/bin
RUN go get -u github.com/folbricht/desync/cmd/desync

# Download busybox
ENV BUSYBOXVERSION=1.30.0-i686
RUN wget https://busybox.net/downloads/binaries/${BUSYBOXVERSION}/busybox
RUN chmod 755 ./busybox

# Download docker
ENV DOCKERVERSION=18.09.2
RUN curl -fsSLO https://download.docker.com/linux/static/stable/x86_64/docker-${DOCKERVERSION}.tgz \
  && tar xzvf docker-${DOCKERVERSION}.tgz --strip 1 \
                 -C /usr/local/bin docker/docker \
  && rm docker-${DOCKERVERSION}.tgz

# Download dropbear
ENV DROPBEAR_VERSION=2018.76
RUN wget http://matt.ucc.asn.au/dropbear/releases/dropbear-${DROPBEAR_VERSION}.tar.bz2
RUN tar xjf dropbear-${DROPBEAR_VERSION}.tar.bz2
RUN apt install -y zlib1g-dev
RUN cd /dropbear-${DROPBEAR_VERSION} && ./configure && make -j4 && cp dbclient /

# Build boot program
COPY ./boot /boot.src
RUN cd /boot.src && make
ADD ./mkimage.sh /mkimage.sh

ENTRYPOINT [ "/bin/bash", "/mkimage.sh" ]
