# syntax=docker/dockerfile:1.7

ARG RUNTIME_BASE=debian:bookworm-slim
FROM ${RUNTIME_BASE}

ARG PLUGIN_EXECUTABLE
ARG RUNTIME_APT_PACKAGES="ca-certificates libtbb12 libhwloc15"

LABEL org.opencontainers.image.vendor="VoltLabs Research"

RUN set -eux; \
    apt-get update; \
    if [ -n "${RUNTIME_APT_PACKAGES}" ]; then \
        # shellcheck disable=SC2086
        apt-get install -y --no-install-recommends ${RUNTIME_APT_PACKAGES}; \
    fi; \
    rm -rf /var/lib/apt/lists/*

COPY .ci-output/install/ /opt/volt/
COPY coretoolkit/scripts/plugin-entrypoint.sh /usr/local/bin/volt-plugin-entrypoint

RUN chmod 0755 /usr/local/bin/volt-plugin-entrypoint

ENV PATH=/opt/volt/bin:${PATH}
ENV VOLT_PLUGIN_EXECUTABLE=${PLUGIN_EXECUTABLE}
WORKDIR /work

ENTRYPOINT ["/usr/local/bin/volt-plugin-entrypoint"]
CMD ["--help"]
