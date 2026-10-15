FROM kicad/kicad:9.0
USER root
RUN apt-get update && apt-get install -y --no-install-recommends openjdk-17-jre xvfb xauth curl ca-certificates && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL -o /opt/freerouting.jar https://github.com/freerouting/freerouting/releases/download/v1.9.0/freerouting-1.9.0.jar
