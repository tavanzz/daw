# The MICINSTATION co-op server (docs/SERVER.md), built from the repository's root:
#
#   docker build -f server/Dockerfile -t micinstation-server .
#
# The program alone, on Ubuntu 24.04, running as a user of its own; everything it keeps
# (sessions, files, keys, its certificate) is in /data.

# JUCE builds its helper (juceaide) while configuring, with its graphics: hence FreeType and
# Fontconfig here, though the server itself draws nothing and links neither.
FROM ubuntu:24.04 AS build
RUN apt-get -o Acquire::Retries=5 update \
 && apt-get -o Acquire::Retries=5 install -y --no-install-recommends cmake ninja-build g++ git ca-certificates \
        pkg-config libfreetype-dev libfontconfig1-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /source
COPY . .
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DDAW_SERVER_ONLY=ON \
 && cmake --build /build --target daw_server_program \
 && strip /build/server/micinstation-server

FROM ubuntu:24.04
LABEL org.opencontainers.image.source="https://github.com/tavanzz/daw" \
      org.opencontainers.image.title="MICINSTATION co-op server" \
      org.opencontainers.image.description="The co-op server of MICINSTATION (docs/SERVER.md)"
RUN useradd --system --home-dir /data --shell /usr/sbin/nologin micinstation \
 && mkdir -p /data \
 && chown micinstation:micinstation /data
COPY --from=build /build/server/micinstation-server /usr/local/bin/micinstation-server
USER micinstation
ENV MICINSTATION_DATA=/data \
    MICINSTATION_PORT=7443
VOLUME /data
EXPOSE 7443
ENTRYPOINT ["micinstation-server"]
