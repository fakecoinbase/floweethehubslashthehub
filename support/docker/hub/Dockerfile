FROM debian:stable
LABEL maintainer="Tom Zander <tomz@freedommail.ch>"
RUN apt-get -y update && apt-get install -y --no-install-recommends \
    libssl1.1 && \
   apt-get clean && \
   rm -rf /var/lib/apt/lists/*

ADD bin /usr/bin
CMD ["/usr/bin/startHub"]

EXPOSE 8332 8334 18332 18333 1235 11235
