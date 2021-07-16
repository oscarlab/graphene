FROM almalinux/almalinux:8

RUN dnf -y update && dnf -y install \
    autoconf \
    bison \
    gawk \
    meson \
    python3-click \
    python3-jinja2 \
&& dnf clean all

# Add the user UID:1001, GID:1001, home at /leeroy
RUN \
    groupadd -r leeroy -g 1001 && \
    useradd -u 1001 -r -g leeroy -m -d /leeroy -c "Leeroy Jenkins" leeroy && \
    chmod 755 /leeroy

# Make sure /leeroy can be written by leeroy
RUN chown 1001 /leeroy

# Blow away any random state
RUN rm -f /leeroy/.rnd

# Make a directory for the intel driver
RUN mkdir -p /opt/intel && chown 1001 /opt/intel

# Set the working directory to leeroy home directory
WORKDIR /leeroy

# Specify the user to execute all commands below
USER leeroy

# Set environment variables.
ENV HOME /leeroy

# Define default command.
CMD ["bash"]
