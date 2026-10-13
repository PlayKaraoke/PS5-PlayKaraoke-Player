# Ambiente de build do PlayKaraoke PS5.
#
# Usa o pacote "ps5-payload-dev" do PacBrew: ele traz o ps5-payload-sdk e
# todas as bibliotecas já compiladas para o PS5 (SDL2, FFmpeg, SQLite, curl,
# minizip...), na mesma versão, instalados em /opt/ps5-payload-sdk.
# A imagem roda nativa no Mac com Apple Silicon (arm64): o SDK só precisa
# de clang/lld 18 no host, os binários gerados são sempre x86_64 do PS5.

FROM ubuntu:24.04

ARG PACBREW_VERSION=v0.41.1
ARG PACBREW_SHA256=e811bc2e4676c5bb7f80a8fe928d2985dce599996a5ca6b54da3e72699287887

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      bash ca-certificates curl make pkgconf socat \
      clang-18 lld-18 llvm-18 \
 && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL -o /tmp/ps5-payload-dev.tar.gz \
      "https://github.com/ps5-payload-dev/pacbrew-repo/releases/download/${PACBREW_VERSION}/ps5-payload-dev.tar.gz" \
 && echo "${PACBREW_SHA256}  /tmp/ps5-payload-dev.tar.gz" | sha256sum -c - \
 && tar xzf /tmp/ps5-payload-dev.tar.gz -C / \
 && rm /tmp/ps5-payload-dev.tar.gz

ENV PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
ENV LLVM_CONFIG=/usr/bin/llvm-config-18
ENV PATH=/opt/ps5-payload-sdk/bin:$PATH

WORKDIR /work
