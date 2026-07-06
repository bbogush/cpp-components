FROM ubuntu:24.04

# Avoid interactive dialogue during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install essential build tools
RUN apt-get update && apt-get install -y \
    build-essential \
    clang-tidy \
    cmake \
    git \
    lcov \
    libboost-dev

# Use a non-root user ubuntu
ARG USERNAME=ubuntu
RUN apt-get update \
    && apt-get install -y sudo \
    && echo ${USERNAME} ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/${USERNAME} \
    && chmod 0440 /etc/sudoers.d/${USERNAME}

USER ${USERNAME}

# Set working directory
WORKDIR /project

# Open shell by default
CMD ["/bin/bash"]
