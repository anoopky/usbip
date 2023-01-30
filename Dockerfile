FROM alpine:latest

COPY usbip /usbip

RUN apk add build-base autoconf \
    automake libtool eudev-dev \
    linux-headers flex bison gmp-dev \
    mpc1-dev mpfr-dev hwdata socat minicom

WORKDIR /usbip

RUN ./autogen.sh
RUN ./configure
RUN make install

CMD ["socat", "-d", "-d", "pty,raw,echo=0", "pty,raw,echo=0"]