FROM us-docker.pkg.dev/shopify-ci/docker-hub-remote/clickhouse/binary-builder@sha256:b4e0be6666539520c3e43b4f0b2e5c56421a1df7464804148eb5545697cd4c30 AS compile
COPY build-source.py /fork-ci/build-source.py
ARG REVISION
RUN python3 /fork-ci/build-source.py --revision "${REVISION}"

FROM us-docker.pkg.dev/shopify-ci/docker-hub-remote/library/ubuntu@sha256:076e7bf82319619f28dd0aa9c02ab34b055ad37100395b4f49a2ffba04fb5e11
ARG DEBIAN_FRONTEND=noninteractive
RUN groupadd --system --gid 101 clickhouse \
    && useradd --system --uid 101 --gid clickhouse --home-dir /var/lib/clickhouse --shell /bin/bash clickhouse \
    && apt-get update \
    && apt-get install --yes --no-install-recommends ca-certificates locales tzdata python3 bash \
    && rm -rf /var/lib/apt/lists/*
COPY --from=compile /fork-output/binary/clickhouse /usr/bin/clickhouse
COPY --from=compile /fork-output/metadata/ /opt/fork-ci/
RUN /usr/bin/clickhouse install --noninteractive --user clickhouse --group clickhouse \
    && test -f /etc/clickhouse-server/config.xml \
    && test -f /etc/clickhouse-server/users.xml \
    && mkdir -p /docker-entrypoint-initdb.d \
    && chmod -R a+rwX /etc/clickhouse-server /etc/clickhouse-client /var/lib/clickhouse /var/log/clickhouse-server
COPY --from=compile /work/source/docker/server/entrypoint.sh /entrypoint.sh
COPY --from=compile /work/source/docker/server/docker_related_config.xml /etc/clickhouse-server/config.d/docker_related_config.xml
ARG REVISION
LABEL org.opencontainers.image.source="https://github.com/Shopify/ClickHouse" \
      org.opencontainers.image.revision="${REVISION}" \
      org.opencontainers.image.description="Shopify ClickHouse source-built development CI image; no deployment"
ENV LANG=C.UTF-8 TZ=UTC CLICKHOUSE_CONFIG=/etc/clickhouse-server/config.xml
EXPOSE 8123 9000 9009
VOLUME /var/lib/clickhouse
ENTRYPOINT ["/entrypoint.sh"]
